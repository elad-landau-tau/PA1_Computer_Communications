// server.cpp
#include "protocol.h"
#include <iostream>
#include <fstream>
#include <vector>
#include <chrono>
#include <thread>
#include <random>
#include <netinet/in.h>
#include <arpa/inet.h>
#include <sys/socket.h>
#include <sys/types.h>
#include <sys/time.h>
#include <unistd.h>

using namespace std;

#define MAX_ATTEMPTS 10

// Sets the source and destiantion IDs of a frame before sending it.
void set_source_dest_id(Frame& frame) {
    // Set the sender ID in the frame header to the process ID
    frame.header.source_id[0] = getpid() & 0xFF;
    frame.header.source_id[1] = (getpid() >> 8) & 0xFF;
    frame.header.source_id[2] = (getpid() >> 16) & 0xFF;
    frame.header.source_id[3] = (getpid() >> 24) & 0xFF;
    frame.header.source_id[4] = 0;
    frame.header.source_id[5] = 0;
    // Set the destination ID in the frame header to the receiver's address randomly
    frame.header.dest_id[0] = rand() % 256;
    frame.header.dest_id[1] = rand() % 256;
    frame.header.dest_id[2] = rand() % 256;
    frame.header.dest_id[3] = rand() % 256;
    frame.header.dest_id[4] = 0;
    frame.header.dest_id[5] = 0;
}

// Checks if the source ID in the frame header matches the current process ID.
bool is_my_source_id(const Frame& frame) {
    return frame.header.source_id[0] == (getpid() & 0xFF) &&
           frame.header.source_id[1] == ((getpid() >> 8) & 0xFF) &&
           frame.header.source_id[2] == ((getpid() >> 16) & 0xFF) &&
           frame.header.source_id[3] == ((getpid() >> 24) & 0xFF);
}

// Gets the IP address and port of the channel.
// Connects to the channel.
// Creates a new socket for communication with the channel,
// and returns its fd.
int connect_to_channel(const char* ip, int port) {
    int sock = socket(AF_INET, SOCK_STREAM, 0);
    sockaddr_in addr{};
    addr.sin_family = AF_INET;
    addr.sin_port = htons(port);
    inet_pton(AF_INET, ip, &addr.sin_addr);
    while (connect(sock, (sockaddr*)&addr, sizeof(addr)) == -1) {}
    return sock;
}

// Gets the channel's socket and a timeout.
// Tries to receive a frame from the channel, giving up after the timeout.
// On success, stores the frame in `output`.
// Returns true on success, or false otherwise.
bool receive_frame(int channel_fd, timeval &timeout, Frame &output) {
    fd_set fds;
    FD_ZERO(&fds);
    FD_SET(channel_fd, &fds);
    int ret = select(channel_fd + 1, &fds, nullptr, nullptr, &timeout);
    if (ret <= 0) return false;
    recv(channel_fd, &output, sizeof output, 0);
    return true;
}

// Waits `time_ms` miliseconds.
// Meanwhile, if any frames arive via the channel (in `channel_fd`),
// this function receives and ignores them.
void wait_and_drop_frames(int time_ms, int channel_fd) {
#ifdef DEBUG
    cout << "wait_and_drop_frames(" << time_ms << ", " << channel_fd << ")" << endl;
#endif
    Frame ignored_frame;
    timeval now, wait_until;
    gettimeofday(&now, nullptr);
    timeval wait_time{0, time_ms * 1000};
    timeradd(&now, &wait_time, &wait_until);
    for (; timercmp(&now, &wait_until, <); gettimeofday(&now, nullptr)) {
        timeval remaining_time;
        timersub(&wait_until, &now, &remaining_time);
        receive_frame(channel_fd, remaining_time, ignored_frame);
    }
    timeval zero_time{0, 0};
    while (receive_frame(channel_fd, zero_time, ignored_frame)) {}
}

// Gets a file.
// Returns its size.
uint64_t get_file_size(ifstream &file) {
    file.seekg(0, ios::end);
    uint64_t file_size = file.tellg();
    file.seekg(0, ios::beg);
    return file_size;
}

// Gets a file.
// Divides its content into a sequence of frames.
// Stores those frames in a vector and returns it.
vector<Frame> file_to_frames(ifstream &file, uint64_t file_size, uint32_t frame_size) {
    vector<Frame> result;
    for (uint32_t i = 0;; i++) {
        Frame frame;
        frame.header.seq_number = i;
        frame.header.payload_length = (uint32_t)min(
            (uint64_t)frame_size,
            file_size - file.tellg()
        );
        set_source_dest_id(frame);
        if (frame.header.payload_length == 0) break;
        file.read(frame.payload, frame.header.payload_length);
        result.push_back(frame);
    }
    return result;
}

// Gets the arguments to the program (argv) after they have been parsed.
// Reads the input file and splits it into frames.
// Sends each frame to the channel using the Aloha-like protocol.
// Prints statistics at the end.
void send_file(const char* ip, int port, const char* filename, int frame_size, int slot_time, int seed, int timeout) {
    // Open file.
    ifstream file(filename, ios::binary);
    if (!file) {
        cerr << "Error: Cannot open file " << filename << endl;
        return;
    }

    // Get file length.
    uint64_t file_size = get_file_size(file);
#ifdef DEBUG
    cout << "Length: " << file_size << endl;
#endif

    // Divide file content to frames and close the file.
    vector<Frame> frames = file_to_frames(file, file_size, frame_size);
    file.close();
#ifdef DEBUG
    for (auto& f : frames) {
        cout << "Payload length: " << f.header.payload_length << endl;
    }
#endif 

    // Connect to the channel.
    int sock = connect_to_channel(ip, port);
#ifdef DEBUG
    cout << "connected" << endl;
#endif

    // Initialize random number generator (for backoff).
    default_random_engine rng(seed);

    // Record the time before the server starts sending.
    auto start = chrono::steady_clock::now();

    // Statistics about sent frames.
    int total_transmissions = 0;
    int max_trans_per_frame = 0;

    // true if all frames were sent successfully, false otherwise.
    bool success = true;

    // Send each frame.
    for (size_t i = 0; i < frames.size(); ++i) {
        Frame& frame = frames[i];
        bool acked = false;

        int attempts;
        // Attempt to send frame until success, up to MAX_ATTEMPTS times.
        for (attempts = 1; attempts <= MAX_ATTEMPTS; attempts++) {
            // Send frame.
            send(sock, &frame, sizeof(FrameHeader) + frame.header.payload_length, 0);

            // Try to receive an ACK.
            Frame response;
            timeval tv{timeout, 0};
            if (
                receive_frame(sock, tv, response) &&
                !is_noise_frame(response) &&
                response.header.seq_number == frame.header.seq_number &&
                is_my_source_id(response)
            ) {
                // ACKED; wait `slot_time` and move on to next frame.
                wait_and_drop_frames(slot_time, sock);
                acked = true;
                break;
            }
            // Not ACKED; use backoff and retry.
            uniform_int_distribution<int> backoff_dist = uniform_int_distribution<int>(0, (1 << min(attempts, 10)) - 1);
            int backoff_time = backoff_dist(rng) * slot_time;
            wait_and_drop_frames(backoff_time, sock);
        }
#ifdef DEBUG
        cout << "Acked: " << acked << endl;
#endif

        // Update statistics after frame was (maybe) sent.
        total_transmissions += attempts;
        max_trans_per_frame = max(max_trans_per_frame, attempts);

        // Stop if frame was not sent.
        if (!acked) {
            success = false;
            break;
        }
    }

    // Calculate total runtime of server.
    auto end = chrono::steady_clock::now();
    int duration = chrono::duration_cast<chrono::milliseconds>(end - start).count();

    // Log the results ('Sent file', 'Result', 'File size', 'Total transfer time', 'Transmissions/frame', 'Average bandwidth').
    cerr << "Sent file: " << filename << endl;
    cerr << "Result: " << (success ? "Success :)" : "Failure :(") << endl;
    cerr << "File size: " << file_size << " Bytes (" << frames.size() << " frames)" << endl;
    cerr << "Total transfer time: " << duration << " milliseconds" << endl;
    cerr << "Transmissions/frame: average " << (double)total_transmissions / frames.size() << ", maximum " << max_trans_per_frame << endl;
    cerr << "Average bandwidth: " << (frames.size() * frames[0].header.payload_length * 8.0) / (duration * 1000.0) << " Mbps" << endl;

    close(sock);
}

int main(int argc, char* argv[]) {
    if (argc != 8) {
        cerr << "Usage: ./my_Server <chan_ip> <chan_port> <file_name> <frame_size> <slot_time> <seed> <timeout>" << endl;
        return 1;
    }
    if (stoi(argv[4]) > MAX_PAYLOAD_SIZE) {
        cerr << "Error: Frame size too large. Maximum is " << MAX_PAYLOAD_SIZE << " bytes." << endl;
        return 1;
    }
    send_file(argv[1], stoi(argv[2]), argv[3], stoi(argv[4]), stoi(argv[5]), stoi(argv[6]), stoi(argv[7]));
    return 0;
}
