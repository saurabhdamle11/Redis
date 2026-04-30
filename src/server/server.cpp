#include "server.h"
#include "resp/resp.h"
#include <arpa/inet.h>
#include <cstring>
#include <iostream>
#include <sys/socket.h>
#include <unistd.h>

void handle_client(int client_fd, const std::unordered_map<std::string, CommandHandler>& commands) {
    char buffer[1024];
    while (true) {
        memset(buffer, 0, sizeof(buffer));
        int bytes = recv(client_fd, buffer, sizeof(buffer), 0);
        if (bytes <= 0) break;

        std::string raw(buffer, bytes);
        auto tokens = parse_resp(raw);
        if (tokens.empty()) continue;

        std::string cmd = tokens[0];
        for (char& c : cmd) c = static_cast<char>(toupper(c));

        auto it = commands.find(cmd);
        std::string response = (it != commands.end())
            ? it->second(tokens)
            : "-ERR unknown command '" + cmd + "'\r\n";
        send(client_fd, response.c_str(), response.size(), 0);
    }
    close(client_fd);
}
