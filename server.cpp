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
#include <unistd.h>

using namespace std;

/**
 * @brief Establishes a connection to a specified IP address and port.
 *
 * This function creates a socket, sets up the address structure, and connects
 * to the specified server using the provided IP address and port number.
 *
 * @param ip The IP address of the server to connect to, as a null-terminated string.
 *           It should be in IPv4 dotted-decimal notation (e.g., "192.168.1.1").
 * @param port The port number of the server to connect to.
 * @return The file descriptor of the connected socket on success, or a negative
 *         value on failure.
 *
 * @note The caller is responsible for closing the socket after use.
 * @note This function does not handle errors; ensure proper error checking
 *       is implemented when using this function.
 */
int connect_to_channel(const char* ip, int port) {
    int sock = socket(AF_INET, SOCK_STREAM, 0);
    sockaddr_in addr{};
    addr.sin_family = AF_INET;
    addr.sin_port = htons(port);
    inet_pton(AF_INET, ip, &addr.sin_addr);
    connect(sock, (sockaddr*)&addr, sizeof(addr));
    return sock;
}

/**
 * @brief Sends a file over a network channel using a custom protocol with retransmissions and backoff.
 * 
 * This function reads a file, splits it into frames, and sends the frames to a specified IP and port.
 * It implements a retransmission mechanism with exponential backoff in case of transmission failures.
 * 
 * @param ip The IP address of the receiver.
 * @param port The port number of the receiver.
 * @param filename The path to the file to be sent.
 * @param frame_size The size of each frame's payload in bytes.
 * @param slot_time The time slot duration for backoff in milliseconds.
 * @param seed The seed for the random number generator used in backoff.
 * @param timeout The timeout duration for waiting for an acknowledgment in seconds.
 * 
 * @details
 * - The file is read in binary mode and split into frames of the specified payload size.
 * - Each frame is sent with a sequence number and a sender ID.
 * - The function waits for an acknowledgment for each frame. If no acknowledgment is received
 *   within the timeout period, the frame is retransmitted with an exponential backoff.
 * - The process stops after 10 retransmission attempts per frame or when all frames are acknowledged.
 * - The function logs the result of the transmission, including success/failure, total transfer time,
 *   average and maximum transmissions per frame, and average bandwidth.
 * 
 * @note
 * - The function assumes the existence of helper functions such as `connect_to_channel`, `is_noise_frame`,
 *   and constants like `MAX_PAYLOAD_SIZE`.
 * - The sender process ID is used as the sender ID in the frame header.
 * - The function uses `std::chrono` for timing and `std::default_random_engine` for random backoff.
 * 
 * @warning
 * - Ensure the file exists and is accessible before calling this function.
 * - The function terminates early if a frame cannot be successfully transmitted after 10 attempts.
 */
void send_file(const char* ip, int port, const char* filename, int frame_size, int slot_time, int seed, int timeout) {
    ifstream file(filename, ios::binary);
    if (!file) {
        cerr << "Error: Cannot open file " << filename << endl;
        return;
    }

    vector<Frame> frames;
    int seq = 0;
    while (!file.eof()) {
        Frame frame;
        frame.header.seq_number = seq++;
        file.read(frame.payload, MAX_PAYLOAD_SIZE);
        frame.header.length = file.gcount();
        frames.push_back(frame);
    }
    file.close();

    int sock = connect_to_channel(ip, port);
    default_random_engine rng(seed);
    uniform_int_distribution<int> backoff_dist;
    auto start = chrono::steady_clock::now();

    int total_transmissions = 0, max_trans_per_frame = 0;
    bool success = true;

    for (size_t i = 0; i < frames.size(); ++i) {
        int attempts = 0;
        Frame& frame = frames[i];
        frame.header.sender_id = getpid(); // Unique sender ID by process ID
        bool acked = false;

        while (attempts < 10) {
            ++attempts;
            send(sock, &frame, sizeof(FrameHeader) + frame.header.length, 0);
            Frame response;
            fd_set fds;
            FD_ZERO(&fds);
            FD_SET(sock, &fds);
            timeval tv{timeout, 0};
            int ret = select(sock + 1, &fds, nullptr, nullptr, &tv);

            if (ret > 0) {
                recv(sock, &response, sizeof(response), 0);
                if (!is_noise_frame(response) && response.header.seq_number == frame.header.seq_number) {
                    acked = true;
                    break;
                }
            }
            backoff_dist = uniform_int_distribution<int>(0, (1 << min(attempts, 10)) - 1);
            int backoff_time = backoff_dist(rng) * slot_time;
            this_thread::sleep_for(chrono::milliseconds(backoff_time));
        }
        total_transmissions += attempts;
        max_trans_per_frame = max(max_trans_per_frame, attempts);
        if (!acked) {
            success = false;
            break;
        }
    }

    auto end = chrono::steady_clock::now();
    int duration = chrono::duration_cast<chrono::milliseconds>(end - start).count();

    cerr << "Sent file " << filename << endl;
    cerr << "Result: " << (success ? "Success :)" : "Failure :(") << endl;
    cerr << "File size: " << frames.size() * MAX_PAYLOAD_SIZE << " Bytes (" << frames.size() << " frames)" << endl;
    cerr << "Total transfer time: " << duration << " milliseconds" << endl;
    cerr << "Transmissions/frame: average " << (double)total_transmissions / frames.size() << ", maximum " << max_trans_per_frame << endl;
    cerr << "Average bandwidth: " << (frames.size() * MAX_PAYLOAD_SIZE * 8.0) / (duration * 1000.0) << " Mbps" << endl;
    close(sock);
}

int main(int argc, char* argv[]) {
    if (argc != 8) {
        cerr << "Usage: ./my_Server <chan_ip> <chan_port> <file_name> <frame_size> <slot_time> <seed> <timeout>" << endl;
        return 1;
    }
    send_file(argv[1], stoi(argv[2]), argv[3], stoi(argv[4]), stoi(argv[5]), stoi(argv[6]), stoi(argv[7]));
    return 0;
}