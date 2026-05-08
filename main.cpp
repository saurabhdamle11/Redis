#include "acl/acl.h"
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

    Acl acl;

    // REDIS_PASSWORD: legacy single-password mode. Tightens the default user.
    if (const char* pw = std::getenv("REDIS_PASSWORD"); pw && *pw) {
        std::string err;
        // resetpass clears nopass; >pw adds the hash.
        acl.set_user("default", {"on", "resetpass", std::string(">") + pw, "~*", "+@all"}, err);
        if (!err.empty()) {
            std::cerr << "Failed to configure default user: " << err << "\n";
            return 1;
        }
        std::cout << "Password authentication enabled for default user.\n";
    }

    // REDIS_ACLFILE: optional path to load/save ACL state.
    if (const char* af = std::getenv("REDIS_ACLFILE"); af && *af) {
        acl.set_aclfile(af);
        std::string err;
        if (!acl.load_from_file(err)) {
            std::cerr << "ACL file load: " << err
                      << " (continuing with built-in default user)\n";
        } else {
            std::cout << "Loaded ACL file: " << af << "\n";
        }
    }

    Store store;
    auto commands = build_command_table(store);

    std::cout << "Listening on port 6379...\n";
    Server server(server_fd, commands, acl);
    server.run();

    close(server_fd);
    return 0;
}
