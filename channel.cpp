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

using namespace std;

struct ClientInfo {
    sockaddr_in addr;
    int sockfd;
    int frames = 0;
    int collisions = 0;
};

vector<ClientInfo> clients;
unordered_map<int, ClientInfo*> sock_to_client;

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

void channel_loop(int port, int slot_time) {
    int listener;
    setup_server(port, listener);
    fd_set fds;
    Frame buffer[FD_SETSIZE];

    while (true) {
        FD_ZERO(&fds);
        FD_SET(listener, &fds);
        int maxfd = listener;
        for (auto& client : clients) {
            FD_SET(client.sockfd, &fds);
            maxfd = max(maxfd, client.sockfd);
        }

        timeval tv{0, slot_time * 1000};
        select(maxfd + 1, &fds, nullptr, nullptr, &tv);

        if (FD_ISSET(listener, &fds)) {
            sockaddr_in cli_addr;
            socklen_t len = sizeof(cli_addr);
            int client_sock = accept(listener, (sockaddr*)&cli_addr, &len);
            fcntl(client_sock, F_SETFL, O_NONBLOCK);
            clients.push_back({cli_addr, client_sock});
            sock_to_client[client_sock] = &clients.back();
        }

        vector<int> ready;
        for (auto& client : clients) {
            if (FD_ISSET(client.sockfd, &fds)) {
                recv(client.sockfd, &buffer[client.sockfd], sizeof(Frame), 0);
                ready.push_back(client.sockfd);
            }
        }

        if (ready.size() == 1) {
            for (auto& client : clients) {
                send(client.sockfd, &buffer[ready[0]], sizeof(FrameHeader) + buffer[ready[0]].header.length, 0);
            }
            sock_to_client[ready[0]]->frames++;
        } else if (ready.size() > 1) {
            Frame noise;
            create_noise_frame(noise);
            for (auto& sock : ready) {
                sock_to_client[sock]->collisions++;
            }
            for (auto& client : clients) {
                send(client.sockfd, &noise, sizeof(noise), 0);
            }
        }
    }
}

int main(int argc, char* argv[]) {
    if (argc != 3) {
        cerr << "Usage: ./my_channel.exe <chan_port> <slot_time>" << endl;
        return 1;
    }
    channel_loop(stoi(argv[1]), stoi(argv[2]));
    return 0;
}