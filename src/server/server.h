#pragma once
#include "types.h"
#include <string>
#include <unordered_map>

struct Connection {
    int fd;
    std::string read_buf;
    std::string write_buf;
    bool authenticated;
};

class Server {
public:
    Server(int listen_fd,
           const std::unordered_map<std::string, CommandHandler>& commands,
           std::string password = {});
    ~Server();
    void run();

private:
    int kq_;
    int listen_fd_;
    const std::unordered_map<std::string, CommandHandler>& commands_;
    std::unordered_map<int, Connection> connections_;
    std::string password_;

    void accept_connection();
    void on_readable(int fd);
    void on_writable(int fd);
    void close_connection(int fd);
    void watch_write(int fd);
    std::string handle_auth(Connection& conn, const std::vector<std::string>& tokens);
    static void set_nonblocking(int fd);
};
