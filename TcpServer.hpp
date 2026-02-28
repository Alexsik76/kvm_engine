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

    bool applyClientOptions() {
        int flag = 1;
        // Disable Nagle's algorithm: send packets immediately without buffering
        if (setsockopt(client_fd, IPPROTO_TCP, TCP_NODELAY,
                       reinterpret_cast<char*>(&flag), sizeof(flag)) < 0) {
            std::cerr << "Warning: Failed to set TCP_NODELAY" << std::endl;
        }
        return true;
    }

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

        // Increase the kernel send buffer to 1 MB to reduce blocking on slow networks
        int sndbuf = 1024 * 1024;
        setsockopt(server_fd, SOL_SOCKET, SO_SNDBUF, &sndbuf, sizeof(sndbuf));

        struct sockaddr_in address = {};
        address.sin_family      = AF_INET;
        address.sin_addr.s_addr = INADDR_ANY;
        address.sin_port        = htons(port);

        if (bind(server_fd, reinterpret_cast<struct sockaddr*>(&address),
                 sizeof(address)) < 0) return false;
        if (listen(server_fd, 1) < 0) return false;

        std::cout << "Waiting for client on port " << port << "..." << std::endl;
        return true;
    }

    // Blocks until a client connects. Call once at startup.
    bool waitForClient() {
        struct sockaddr_in client_addr;
        socklen_t addr_len = sizeof(client_addr);
        client_fd = accept(server_fd,
                           reinterpret_cast<struct sockaddr*>(&client_addr),
                           &addr_len);
        if (client_fd < 0) return false;

        applyClientOptions();
        std::cout << "Client connected! Starting stream..." << std::endl;
        return true;
    }

    // Close the current client and wait for the next one (reconnect support).
    bool waitForNextClient() {
        if (client_fd != -1) {
            close(client_fd);
            client_fd = -1;
        }
        std::cout << "Waiting for next client on port " << port << "..." << std::endl;
        return waitForClient();
    }

    // Returns false when client disconnects; caller should call waitForNextClient().
    bool sendData(const void* data, size_t size) {
        if (client_fd == -1) return false;

        size_t      total_sent = 0;
        const char* ptr        = static_cast<const char*>(data);

        while (total_sent < size) {
            ssize_t sent = send(client_fd, ptr + total_sent,
                                size - total_sent, MSG_NOSIGNAL);
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

#endif // TCP_SERVER_HPP