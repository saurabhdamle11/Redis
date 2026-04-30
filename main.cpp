#include "commands/commands.h"
#include "server/server.h"
#include "store/store.h"
#include <arpa/inet.h>
#include <iostream>
#include <sys/socket.h>
#include <thread>
#include <unistd.h>

int main() {
    std::cout << std::unitbuf;
    std::cerr << std::unitbuf;

    int server_fd = socket(AF_INET, SOCK_STREAM, 0);
    if (server_fd < 0) {
        std::cerr << "Failed to create server socket\n";
        return 1;
    }

    int reuse = 1;
    if (setsockopt(server_fd, SOL_SOCKET, SO_REUSEADDR, &reuse, sizeof(reuse)) < 0) {
        std::cerr << "setsockopt failed\n";
        return 1;
    }

    struct sockaddr_in addr{};
    addr.sin_family      = AF_INET;
    addr.sin_addr.s_addr = INADDR_ANY;
    addr.sin_port        = htons(6379);

    if (bind(server_fd, reinterpret_cast<struct sockaddr*>(&addr), sizeof(addr)) != 0) {
        std::cerr << "Failed to bind to port 6379\n";
        return 1;
    }

    if (listen(server_fd, 5) != 0) {
        std::cerr << "listen failed\n";
        return 1;
    }

    Store store;
    auto commands = build_command_table(store);

    std::cout << "Listening on port 6379...\n";

    while (true) {
        struct sockaddr_in client_addr{};
        socklen_t client_len = sizeof(client_addr);
        int client_fd = accept(server_fd, reinterpret_cast<struct sockaddr*>(&client_addr), &client_len);
        if (client_fd < 0) {
            std::cerr << "Accept failed\n";
            continue;
        }
        std::cout << "Client connected\n";
        std::thread([client_fd, &commands] { handle_client(client_fd, commands); }).detach();
    }

    close(server_fd);
    return 0;
}
