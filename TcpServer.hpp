#ifndef TCP_SERVER_HPP
#define TCP_SERVER_HPP

#include <iostream>
#include <sys/socket.h>
#include <netinet/in.h>
#include <netinet/tcp.h>
#include <unistd.h>
#include <cstring>

class TcpServer {
private:
    int server_fd;
    int client_fd;
    int port;

public:
    TcpServer(int p) : server_fd(-1), client_fd(-1), port(p) {}

    ~TcpServer() {
        if (client_fd != -1) close(client_fd);
        if (server_fd != -1) close(server_fd);
        std::cout << "TCP Server closed." << std::endl;
    }

    bool start() {
        server_fd = socket(AF_INET, SOCK_STREAM, 0);
        if (server_fd == -1) return false;

        int opt = 1;
        setsockopt(server_fd, SOL_SOCKET, SO_REUSEADDR, &opt, sizeof(opt));

        struct sockaddr_in address = {};
        address.sin_family = AF_INET;
        address.sin_addr.s_addr = INADDR_ANY;
        address.sin_port = htons(port);

        if (bind(server_fd, (struct sockaddr*)&address, sizeof(address)) < 0) return false;
        if (listen(server_fd, 1) < 0) return false;

        std::cout << "Waiting for client on port " << port << "..." << std::endl;
        return true;
    }

    bool waitForClient() {
        struct sockaddr_in client_addr;
        socklen_t addr_len = sizeof(client_addr);
        client_fd = accept(server_fd, (struct sockaddr*)&client_addr, &addr_len);
        if (client_fd < 0) return false;
        // Disable Nagle's algorithm for low-latency video streaming
        int flag = 1;
        if (setsockopt(client_fd, IPPROTO_TCP, TCP_NODELAY, (char *)&flag, sizeof(int)) < 0) {
            std::cerr << "Warning: Failed to set TCP_NODELAY" << std::endl;
        }
        std::cout << "Client connected! Starting stream..." << std::endl;
        return true;
    }

    bool sendData(const void* data, size_t size) {
        if (client_fd == -1) return false;
        
        size_t total_sent = 0;
        const char* ptr = static_cast<const char*>(data);
        
        while (total_sent < size) {
            ssize_t sent = send(client_fd, ptr + total_sent, size - total_sent, MSG_NOSIGNAL);
            if (sent <= 0) {
                close(client_fd);
                client_fd = -1;
                return false;
            }
            total_sent += sent;
        }
        return true;
    }
};

#endif