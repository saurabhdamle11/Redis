#include "commands/commands.h"
#include "server/server.h"
#include "store/store.h"
#include <arpa/inet.h>
#include <cstdlib>
#include <iostream>
#include <sys/socket.h>
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
    setsockopt(server_fd, SOL_SOCKET, SO_REUSEADDR, &reuse, sizeof(reuse));

    struct sockaddr_in addr{};
    addr.sin_family      = AF_INET;
    addr.sin_addr.s_addr = INADDR_ANY;
    addr.sin_port        = htons(6379);

    if (bind(server_fd, reinterpret_cast<struct sockaddr*>(&addr), sizeof(addr)) != 0) {
        std::cerr << "Failed to bind to port 6379\n";
        return 1;
    }

    if (listen(server_fd, 128) != 0) {
        std::cerr << "listen failed\n";
        return 1;
    }

    const char* pw_env = std::getenv("REDIS_PASSWORD");
    std::string password = pw_env ? pw_env : "";
    if (!password.empty())
        std::cout << "Password authentication enabled.\n";

    Store store;
    auto commands = build_command_table(store);

    std::cout << "Listening on port 6379...\n";
    Server server(server_fd, commands, password);
    server.run();

    close(server_fd);
    return 0;
}
