// channel.cpp
#include "protocol.h"
#include <iostream>
#include <vector>
#include <thread>
#include <chrono>
#include <sys/socket.h>
#include <netinet/in.h>
#include <unistd.h>
#include <fcntl.h>
#include <arpa/inet.h>
#include <sys/select.h>
#include <signal.h>

using namespace std;

// Information about a server currently or previously connected to this channel.
struct ServerInfo {
    sockaddr_in addr;
    int sockfd;
    int frames = 0;
    int collisions = 0;
    bool is_dead = false;
};

// All the servers that have ever connected to the channel.
vector<ServerInfo> servers;

// Gets a port number.
// Creates a listening socket to listen for incoming connections in that port.
// Returns the socket.
int setup_server(int port) {
    int listener = socket(AF_INET, SOCK_STREAM, 0);
    int opt = 1;
    setsockopt(listener, SOL_SOCKET, SO_REUSEADDR, &opt, sizeof(opt));

    sockaddr_in addr{};
    addr.sin_family = AF_INET;
    addr.sin_port = htons(port);
    addr.sin_addr.s_addr = INADDR_ANY;

    bind(listener, (sockaddr*)&addr, sizeof(addr));
    listen(listener, 128);
    fcntl(listener, F_SETFL, O_NONBLOCK);
    return listener;
}

// Gets the arguments to the program (argv), already converted to ints.
// Runs a channel.
// Stores statistics about received and sent frames.
// This function returns when the user pressed CTRL+D (EOF).
void channel_loop(int port, int slot_time) {
    // Create listening port.
    int listener = setup_server(port);

    // Change stdin mode to non-blocking.
    fcntl(STDIN_FILENO, F_SETFL, O_NONBLOCK);

    // Repeatedly listen for requests and serve them (unless there are collisions).
    while (true) {
        // Create the set of fd's to which we want to listen.
        // These fd's are: stdin, the listening socket, and the servers' sockets.
        fd_set fds;
        FD_ZERO(&fds);
        FD_SET(STDIN_FILENO, &fds);
        FD_SET(listener, &fds);
        int maxfd = max(listener, STDIN_FILENO);
        for (auto& server : servers) {
            if (server.is_dead) continue;
            FD_SET(server.sockfd, &fds);
            maxfd = max(maxfd, server.sockfd);
        }

        // Listen to fd's for slot_time.
        timeval tv{0, slot_time * 1000};
        int num_ready = select(maxfd + 1, &fds, nullptr, nullptr, &tv);
        if (num_ready == 0) continue;
#ifdef DEBUG
        cout << "ready: " << num_ready << endl;
#endif

        // If got EOF in stdin, stop program.
        if (FD_ISSET(STDIN_FILENO, &fds)) {
            char buff;
            if (read(STDIN_FILENO, &buff, sizeof buff) == 0) {
                break;
            }
        }

        // If the listener got a new server, add it to the list of servers.
        if (FD_ISSET(listener, &fds)) {
            sockaddr_in cli_addr;
            socklen_t len = sizeof(cli_addr);
            int server_sock = accept(listener, (sockaddr*)&cli_addr, &len);
            fcntl(server_sock, F_SETFL, O_NONBLOCK);
            servers.push_back({cli_addr, server_sock});
        }

        // Receive frames from all servers that sent a frame.
        // Create a vector of servers that sent a frame.
        Frame received_frame;
        vector<ServerInfo*> ready;
        for (auto &server : servers) {
            if (server.is_dead || !FD_ISSET(server.sockfd, &fds)) continue;
            int res = recv(server.sockfd, &received_frame, sizeof(Frame), 0);
            if (res == 0) {
                server.is_dead = true;
                continue;
            }
            ready.push_back(&server);
        }

        // If exactly one frame was received, there is no collision.
        if (ready.size() == 1) {
            // Resend frame to all connected (and alive) servers.
            for (auto& server : servers) {
                if (server.is_dead) continue;
#ifdef DEBUG
                static int num_acks;
                num_acks++;
                cout << "Going to send ACK no. " << num_acks << endl;
#endif
                send(server.sockfd, &received_frame, sizeof(FrameHeader) + received_frame.header.payload_length, 0);
            }
            // Increment frame count on the sending server.
            ready[0]->frames++;
        }
        // If more than one frame was received, there is a collision.
        else if (ready.size() > 1) {
            // Increment collision count on all servers that participated in the collision.
            for (auto& server : ready) {
                if (server->is_dead) continue;
                server->collisions++;
            }
            // Send a noise frame to everyone.
            Frame noise;
            create_noise_frame(noise);
            for (auto& server : servers) {
                if (server.is_dead) continue;
                send(server.sockfd, &noise, sizeof(noise), 0);
            }
        }
    }
}

// Display statistics about received frames and collisions.
void report_stats() {
    for (auto& server : servers) {
        char ip_str[INET_ADDRSTRLEN];
        inet_ntop(AF_INET, &server.addr.sin_addr, ip_str, sizeof(ip_str));
        cerr << "From " << ip_str << " port " << ntohs(server.addr.sin_port)
           << ": " /*<< server.frames << " frames, " */<< server.collisions << " collisions" << endl;
    }
#ifdef DEBUG
    cout << "end of report" << endl;
#endif
    exit(0);
}

int main(int argc, char* argv[]) {
    if (argc != 3) {
        cerr << "Usage: ./my_channel.exe <chan_port> <slot_time>" << endl;
        return 1;
    }
    signal(SIGPIPE, SIG_IGN);
    channel_loop(stoi(argv[1]), stoi(argv[2]));
    report_stats();
    return 0;
}
