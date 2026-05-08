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
#include <sstream>

namespace {

std::string upper(std::string s) {
    for (char& c : s) c = static_cast<char>(std::toupper(static_cast<unsigned char>(c)));
    return s;
}

std::string lower(std::string s) {
    for (char& c : s) c = static_cast<char>(std::tolower(static_cast<unsigned char>(c)));
    return s;
}

std::string bulk(const std::string& s) {
    return "$" + std::to_string(s.size()) + "\r\n" + s + "\r\n";
}

std::string array_of_bulks(const std::vector<std::string>& items) {
    std::string out = "*" + std::to_string(items.size()) + "\r\n";
    for (const auto& s : items) out += bulk(s);
    return out;
}

}  // namespace

void Server::set_nonblocking(int fd) {
    int flags = fcntl(fd, F_GETFL, 0);
    fcntl(fd, F_SETFL, flags | O_NONBLOCK);
}

Server::Server(int listen_fd,
               const std::unordered_map<std::string, CommandHandler>& commands,
               Acl& acl)
    : listen_fd_(listen_fd), commands_(commands), acl_(acl) {
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
    std::string username, password;
    if (tokens.size() == 2) {
        username = "default";
        password = tokens[1];
    } else if (tokens.size() == 3) {
        username = tokens[1];
        password = tokens[2];
    } else {
        return "-ERR wrong number of arguments for AUTH\r\n";
    }

    auto resolved = acl_.authenticate(username, password);
    if (!resolved) {
        conn.authenticated = false;
        return "-WRONGPASS invalid username-password pair or user is disabled.\r\n";
    }
    conn.username = *resolved;
    conn.authenticated = true;
    return "+OK\r\n";
}

// Commands that take no key, or whose key positions need command-specific parsing.
std::vector<std::string> Server::extract_keys(const std::string& cmd_upper,
                                              const std::vector<std::string>& args) {
    if (cmd_upper == "PING" || cmd_upper == "ECHO" ||
        cmd_upper == "AUTH" || cmd_upper == "ACL") {
        return {};
    }
    if (cmd_upper == "XREAD") {
        size_t streams_idx = std::string::npos;
        for (size_t i = 1; i < args.size(); ++i) {
            if (upper(args[i]) == "STREAMS") { streams_idx = i; break; }
        }
        if (streams_idx == std::string::npos) return {};
        size_t remaining = args.size() - streams_idx - 1;
        size_t n = remaining / 2;
        std::vector<std::string> keys;
        keys.reserve(n);
        for (size_t i = 0; i < n; ++i) keys.push_back(args[streams_idx + 1 + i]);
        return keys;
    }
    // SET, GET, RPUSH, LPUSH, LLEN, LRANGE, TYPE, BLPOP, XADD, XRANGE — all key at args[1].
    if (args.size() < 2) return {};
    return {args[1]};
}

std::string Server::handle_acl(Connection& conn, const std::vector<std::string>& tokens) {
    if (tokens.size() < 2) {
        return "-ERR wrong number of arguments for 'acl' command\r\n";
    }
    std::string sub = upper(tokens[1]);

    if (sub == "WHOAMI") {
        return bulk(conn.username);
    }

    if (sub == "LIST") {
        auto names = acl_.list_users();
        std::vector<std::string> lines;
        lines.reserve(names.size());
        for (const auto& n : names) {
            auto u = acl_.get_user(n);
            if (u) lines.push_back(Acl::format_user(*u));
        }
        return array_of_bulks(lines);
    }

    if (sub == "USERS") {
        return array_of_bulks(acl_.list_users());
    }

    if (sub == "CAT") {
        if (tokens.size() == 2) {
            return array_of_bulks(Acl::all_categories());
        }
        const auto& cmds = Acl::commands_in_category(lower(tokens[2]));
        std::vector<std::string> v(cmds.begin(), cmds.end());
        std::sort(v.begin(), v.end());
        return array_of_bulks(v);
    }

    if (sub == "GETUSER") {
        if (tokens.size() < 3) return "-ERR wrong number of arguments for 'acl|getuser'\r\n";
        auto u = acl_.get_user(tokens[2]);
        if (!u) return "*-1\r\n";

        std::vector<std::string> flags;
        flags.push_back(u->enabled ? "on" : "off");
        if (u->allkeys)     flags.push_back("allkeys");
        if (u->allcommands) flags.push_back("allcommands");
        if (u->nopass)      flags.push_back("nopass");

        std::vector<std::string> passwords;
        for (const auto& h : u->password_hashes) passwords.push_back(h);

        std::vector<std::string> keys;
        if (u->allkeys) keys.push_back("*");
        else for (const auto& p : u->key_patterns) keys.push_back(p);

        std::ostringstream cmds;
        cmds << (u->allcommands ? "+@all" : "-@all");
        for (const auto& c : u->allowed) cmds << " +" << c;
        for (const auto& c : u->denied)  cmds << " -" << c;

        std::string out = "*8\r\n";
        out += bulk("flags");     out += array_of_bulks(flags);
        out += bulk("passwords"); out += array_of_bulks(passwords);
        out += bulk("commands");  out += bulk(cmds.str());
        out += bulk("keys");      out += array_of_bulks(keys);
        return out;
    }

    if (sub == "SETUSER") {
        if (tokens.size() < 3) return "-ERR wrong number of arguments for 'acl|setuser'\r\n";
        std::vector<std::string> rules(tokens.begin() + 3, tokens.end());
        std::string err;
        if (!acl_.set_user(tokens[2], rules, err)) return "-ERR " + err + "\r\n";
        return "+OK\r\n";
    }

    if (sub == "DELUSER") {
        if (tokens.size() < 3) return "-ERR wrong number of arguments for 'acl|deluser'\r\n";
        int removed = 0;
        for (size_t i = 2; i < tokens.size(); ++i) {
            if (acl_.del_user(tokens[i])) ++removed;
        }
        return ":" + std::to_string(removed) + "\r\n";
    }

    if (sub == "SAVE") {
        std::string err;
        if (!acl_.save_to_file(err)) return "-ERR " + err + "\r\n";
        return "+OK\r\n";
    }

    if (sub == "LOAD") {
        std::string err;
        if (!acl_.load_from_file(err)) return "-ERR " + err + "\r\n";
        return "+OK\r\n";
    }

    return "-ERR Unknown ACL subcommand '" + tokens[1] + "'\r\n";
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
        // Start authenticated as `default` only when the default user is open
        // (on + nopass). Otherwise the client must AUTH first.
        Connection conn{fd, {}, {}, acl_.default_user_is_open(), "default"};
        connections_[fd] = std::move(conn);

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
        if (tokens.empty()) { tokens.clear(); continue; }

        std::string cmd_upper = upper(tokens[0]);

        if (cmd_upper == "AUTH") {
            conn.write_buf += handle_auth(conn, tokens);
            tokens.clear();
            continue;
        }

        // Re-validate the user every dispatch — SETUSER/DELUSER on another
        // connection may have disabled or deleted them mid-session.
        if (!acl_.user_enabled(conn.username)) conn.authenticated = false;

        if (!conn.authenticated) {
            conn.write_buf += "-NOAUTH Authentication required.\r\n";
            tokens.clear();
            continue;
        }

        if (!acl_.command_allowed(conn.username, cmd_upper)) {
            conn.write_buf += "-NOPERM User " + conn.username
                            + " has no permissions to run the '"
                            + lower(cmd_upper) + "' command\r\n";
            tokens.clear();
            continue;
        }

        auto keys = extract_keys(cmd_upper, tokens);
        if (!keys.empty() && !acl_.keys_allowed(conn.username, keys)) {
            conn.write_buf += "-NOPERM User " + conn.username
                            + " has no permissions to access one of the keys"
                              " used as arguments\r\n";
            tokens.clear();
            continue;
        }

        if (cmd_upper == "ACL") {
            conn.write_buf += handle_acl(conn, tokens);
        } else {
            auto cit = commands_.find(cmd_upper);
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
