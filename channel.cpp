// channel.cpp
#include "protocol.h"
#include <iostream>
#include <vector>
#include <unordered_map>
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

struct ServerInfo {
    sockaddr_in addr;
    int sockfd;
    int frames = 0;
    int collisions = 0;
};

vector<ServerInfo> servers;
unordered_map<int, ServerInfo*> sock_to_server;

/**
 * @brief Sets up a server socket to listen for incoming connections.
 * 
 * This function creates a non-blocking server socket, binds it to the specified
 * port, and prepares it to listen for incoming server connections.
 * 
 * @param port The port number on which the server will listen for connections.
 * @param listener A reference to an integer where the created server socket's
 *                 file descriptor will be stored.
 * 
 * @note The socket is configured with the SO_REUSEADDR option to allow reuse
 *       of local addresses. The listener socket is set to non-blocking mode.
 * 
 * @throws This function does not explicitly handle errors. It is the caller's
 *         responsibility to check for errors in socket creation, binding, and
 *         other operations.
 */
void setup_server(int port, int& listener) {
    listener = socket(AF_INET, SOCK_STREAM, 0);
    int opt = 1;
    setsockopt(listener, SOL_SOCKET, SO_REUSEADDR, &opt, sizeof(opt));

    sockaddr_in addr{};
    addr.sin_family = AF_INET;
    addr.sin_port = htons(port);
    addr.sin_addr.s_addr = INADDR_ANY;

    bind(listener, (sockaddr*)&addr, sizeof(addr));
    listen(listener, 10);
    fcntl(listener, F_SETFL, O_NONBLOCK);
}

/**
 * @brief Handles the main loop for a communication channel, managing server connections,
 *        receiving frames, and broadcasting frames or noise based on channel activity.
 * 
 * @param port The port number on which the server listens for incoming connections.
 * @param slot_time The time slot duration (in milliseconds) used for the select timeout.
 * 
 * This function performs the following tasks:
 * - Sets up a server socket to listen for incoming server connections.
 * - Uses the `select` system call to monitor multiple sockets for activity.
 * - Accepts new server connections and adds them to the list of managed servers.
 * - Receives frames from servers and determines whether to broadcast the frame or
 *   send a noise frame in case of collisions.
 * - Tracks the number of frames successfully sent and collisions for each server.
 * 
 * Behavior:
 * - If only one server sends a frame during a time slot, the frame is broadcast to all servers.
 * - If multiple servers send frames simultaneously, a noise frame is sent to all servers,
 *   and collisions are recorded for the involved servers.
 * 
 * Note:
 * - The function runs indefinitely in a loop and must be terminated externally.
 * - Non-blocking sockets are used for server connections.
 */
void channel_loop(int port, int slot_time) {
    int listener;
    setup_server(port, listener);
    fd_set fds;
    Frame buffer[FD_SETSIZE];

    while (true) {
        FD_ZERO(&fds);
        FD_SET(listener, &fds);
        int maxfd = listener;
        for (auto& server : servers) {
            FD_SET(server.sockfd, &fds);
            maxfd = max(maxfd, server.sockfd);
        }

        timeval tv{0, slot_time * 1000};
        select(maxfd + 1, &fds, nullptr, nullptr, &tv);

        if (FD_ISSET(listener, &fds)) {
            sockaddr_in cli_addr;
            socklen_t len = sizeof(cli_addr);
            int server_sock = accept(listener, (sockaddr*)&cli_addr, &len);
            fcntl(server_sock, F_SETFL, O_NONBLOCK);
            servers.push_back({cli_addr, server_sock});
            sock_to_server[server_sock] = &servers.back();
        }

        vector<int> ready;
        for (auto& server : servers) {
            if (FD_ISSET(server.sockfd, &fds)) {
                recv(server.sockfd, &buffer[server.sockfd], sizeof(Frame), 0);
                ready.push_back(server.sockfd);
            }
        }

        if (ready.size() == 1) {
            for (auto& server : servers) {
                send(server.sockfd, &buffer[ready[0]], sizeof(FrameHeader) + buffer[ready[0]].header.payload_length, 0);
            }
            sock_to_server[ready[0]]->frames++;
        } else if (ready.size() > 1) {
            Frame noise;
            create_noise_frame(noise);
            for (auto& sock : ready) {
                sock_to_server[sock]->collisions++;
            }
            for (auto& server : servers) {
                send(server.sockfd, &noise, sizeof(noise), 0);
            }
        }
    }
}

void on_ctrl_c(int signal) {
    (void)signal;
    for (auto& server : servers) {
        cout << "From ?.?.?.? port ????: " << server.frames << " frames, " << server.collisions << " collisions" << endl;
    }
    exit(0);
}

int main(int argc, char* argv[]) {
    if (argc != 3) {
        cerr << "Usage: ./my_channel.exe <chan_port> <slot_time>" << endl;
        return 1;
    }
    struct sigaction ctrl_c_action = {};
    ctrl_c_action.sa_handler = on_ctrl_c;
    sigemptyset(&ctrl_c_action.sa_mask);
    ctrl_c_action.sa_flags = 0;
    sigaction(SIGINT, &ctrl_c_action, nullptr);
    channel_loop(stoi(argv[1]), stoi(argv[2]));
    return 0;
}
