#include "server.h"
#include "resp/resp.h"
#include <sys/event.h>
#include <sys/socket.h>
#include <arpa/inet.h>
#include <fcntl.h>
#include <unistd.h>
#include <cerrno>
#include <cctype>
#include <cstring>
#include <iostream>

void Server::set_nonblocking(int fd) {
    int flags = fcntl(fd, F_GETFL, 0);
    fcntl(fd, F_SETFL, flags | O_NONBLOCK);
}

Server::Server(int listen_fd,
               const std::unordered_map<std::string, CommandHandler>& commands,
               std::string password)
    : listen_fd_(listen_fd), commands_(commands), password_(std::move(password)) {
    kq_ = kqueue();
    if (kq_ < 0) {
        std::cerr << "kqueue() failed: " << strerror(errno) << "\n";
        std::exit(1);
    }

    set_nonblocking(listen_fd_);

    struct kevent ev;
    EV_SET(&ev, listen_fd_, EVFILT_READ, EV_ADD, 0, 0, nullptr);
    kevent(kq_, &ev, 1, nullptr, 0, nullptr);
}

Server::~Server() {
    close(kq_);
}

// Register a one-shot write-readiness event. Fires once when the socket
// send buffer has space, then removes itself automatically.
void Server::watch_write(int fd) {
    struct kevent ev;
    EV_SET(&ev, fd, EVFILT_WRITE, EV_ADD | EV_ONESHOT, 0, 0, nullptr);
    kevent(kq_, &ev, 1, nullptr, 0, nullptr);
}

std::string Server::handle_auth(Connection& conn, const std::vector<std::string>& tokens) {
    if (password_.empty())
        return "-ERR Client sent AUTH, but no password is set.\r\n";
    if (tokens.size() < 2)
        return "-ERR wrong number of arguments for AUTH\r\n";
    if (tokens[1] == password_) {
        conn.authenticated = true;
        return "+OK\r\n";
    }
    conn.authenticated = false;
    return "-ERR invalid username-password pair or user is disabled.\r\n";
}

void Server::accept_connection() {
    while (true) {
        struct sockaddr_in addr{};
        socklen_t len = sizeof(addr);
        int fd = accept(listen_fd_, reinterpret_cast<struct sockaddr*>(&addr), &len);
        if (fd < 0) {
            if (errno == EAGAIN || errno == EWOULDBLOCK) break;
            std::cerr << "accept failed: " << strerror(errno) << "\n";
            break;
        }

        set_nonblocking(fd);
        // Start authenticated only when no password is configured.
        connections_[fd] = Connection{fd, {}, {}, password_.empty()};

        struct kevent ev;
        EV_SET(&ev, fd, EVFILT_READ, EV_ADD, 0, 0, nullptr);
        kevent(kq_, &ev, 1, nullptr, 0, nullptr);

        std::cout << "Client connected: fd=" << fd << "\n";
    }
}

void Server::on_readable(int fd) {
    auto it = connections_.find(fd);
    if (it == connections_.end()) return;
    Connection& conn = it->second;

    // Drain the socket into the read buffer.
    char buf[4096];
    while (true) {
        ssize_t n = recv(fd, buf, sizeof(buf), 0);
        if (n > 0) {
            conn.read_buf.append(buf, n);
        } else if (n == 0) {
            close_connection(fd);
            return;
        } else {
            if (errno == EAGAIN || errno == EWOULDBLOCK) break;
            close_connection(fd);
            return;
        }
    }

    // Parse and dispatch every complete command in the read buffer.
    std::vector<std::string> tokens;
    while (try_parse_resp(conn.read_buf, tokens)) {
        if (tokens.empty()) continue;

        std::string cmd = tokens[0];
        for (char& c : cmd) c = static_cast<char>(toupper(c));

        if (cmd == "AUTH") {
            conn.write_buf += handle_auth(conn, tokens);
        } else if (!conn.authenticated) {
            conn.write_buf += "-NOAUTH Authentication required.\r\n";
        } else {
            auto cit = commands_.find(cmd);
            conn.write_buf += (cit != commands_.end())
                ? cit->second(tokens)
                : "-ERR unknown command '" + tokens[0] + "'\r\n";
        }

        tokens.clear();
    }

    if (!conn.write_buf.empty())
        on_writable(fd);
}

void Server::on_writable(int fd) {
    auto it = connections_.find(fd);
    if (it == connections_.end()) return;
    Connection& conn = it->second;

    while (!conn.write_buf.empty()) {
        ssize_t n = send(fd, conn.write_buf.data(), conn.write_buf.size(), 0);
        if (n > 0) {
            conn.write_buf.erase(0, static_cast<size_t>(n));
        } else if (n < 0) {
            if (errno == EAGAIN || errno == EWOULDBLOCK) {
                // Kernel send buffer full — wait for writability.
                watch_write(fd);
                return;
            }
            close_connection(fd);
            return;
        }
    }
}

void Server::close_connection(int fd) {
    struct kevent ev;
    EV_SET(&ev, fd, EVFILT_READ, EV_DELETE, 0, 0, nullptr);
    kevent(kq_, &ev, 1, nullptr, 0, nullptr);
    close(fd);
    connections_.erase(fd);
    std::cout << "Client disconnected: fd=" << fd << "\n";
}

void Server::run() {
    struct kevent events[64];
    std::cout << "Event loop running (kqueue)...\n";

    while (true) {
        int n = kevent(kq_, nullptr, 0, events, 64, nullptr);
        if (n < 0) {
            if (errno == EINTR) continue;
            std::cerr << "kevent wait failed: " << strerror(errno) << "\n";
            break;
        }

        for (int i = 0; i < n; ++i) {
            int fd = static_cast<int>(events[i].ident);

            if (events[i].flags & EV_ERROR) {
                std::cerr << "kevent error on fd=" << fd << ": "
                          << strerror(static_cast<int>(events[i].data)) << "\n";
                if (fd != listen_fd_) close_connection(fd);
                continue;
            }

            if (fd == listen_fd_) {
                accept_connection();
            } else if (events[i].filter == EVFILT_READ) {
                on_readable(fd);
            } else if (events[i].filter == EVFILT_WRITE) {
                on_writable(fd);
            }
        }
    }
}
