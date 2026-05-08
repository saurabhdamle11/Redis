#pragma once
#include "acl/acl.h"
#include "types.h"
#include <string>
#include <unordered_map>

struct Connection {
    int fd;
    std::string read_buf;
    std::string write_buf;
    bool authenticated;
    std::string username;
};

class Server {
public:
    Server(int listen_fd,
           const std::unordered_map<std::string, CommandHandler>& commands,
           Acl& acl);
    ~Server();
    void run();

private:
    int kq_;
    int listen_fd_;
    const std::unordered_map<std::string, CommandHandler>& commands_;
    std::unordered_map<int, Connection> connections_;
    Acl& acl_;

    void accept_connection();
    void on_readable(int fd);
    void on_writable(int fd);
    void close_connection(int fd);
    void watch_write(int fd);

    std::string handle_auth(Connection& conn, const std::vector<std::string>& tokens);
    std::string handle_acl(Connection& conn, const std::vector<std::string>& tokens);

    static std::vector<std::string> extract_keys(const std::string& cmd_upper,
                                                  const std::vector<std::string>& args);
    static void set_nonblocking(int fd);
};
