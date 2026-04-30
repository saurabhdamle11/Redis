#include "commands.h"
#include <chrono>
#include <cctype>

std::unordered_map<std::string, CommandHandler> build_command_table(Store& store) {
    return {
        {"PING", [](const Args&) -> std::string {
            return "+PONG\r\n";
        }},

        {"ECHO", [](const Args& args) -> std::string {
            if (args.size() < 2) return "-ERR wrong number of arguments for ECHO\r\n";
            const std::string& msg = args[1];
            return "$" + std::to_string(msg.size()) + "\r\n" + msg + "\r\n";
        }},

        {"GET", [&store](const Args& args) -> std::string {
            if (args.size() < 2) return "-ERR wrong number of arguments for GET\r\n";
            auto val = store.get(args[1]);
            if (!val) return "$-1\r\n";
            return "$" + std::to_string(val->size()) + "\r\n" + *val + "\r\n";
        }},

        {"SET", [&store](const Args& args) -> std::string {
            if (args.size() < 3) return "-ERR wrong number of arguments for SET\r\n";
            std::optional<std::chrono::milliseconds> ttl;
            if (args.size() >= 5) {
                std::string flag = args[3];
                for (char& c : flag) c = static_cast<char>(toupper(c));
                long long duration = std::stoll(args[4]);
                if (flag == "EX")
                    ttl = std::chrono::milliseconds(duration * 1000);
                else if (flag == "PX")
                    ttl = std::chrono::milliseconds(duration);
            }
            store.set(args[1], args[2], ttl);
            return "+OK\r\n";
        }},

        {"RPUSH", [&store](const Args& args) -> std::string {
            if (args.size() < 3) return "-ERR wrong number of arguments for RPUSH\r\n";
            std::vector<std::string> values(args.begin() + 2, args.end());
            int n = store.rpush(args[1], values);
            return ":" + std::to_string(n) + "\r\n";
        }},

        {"LPUSH", [&store](const Args& args) -> std::string {
            if (args.size() < 3) return "-ERR wrong number of arguments for LPUSH\r\n";
            std::vector<std::string> values(args.begin() + 2, args.end());
            int n = store.lpush(args[1], values);
            return ":" + std::to_string(n) + "\r\n";
        }},

        {"LLEN", [&store](const Args& args) -> std::string {
            if (args.size() < 2) return "-ERR wrong number of arguments for LLEN\r\n";
            return ":" + std::to_string(store.llen(args[1])) + "\r\n";
        }},

        {"LRANGE", [&store](const Args& args) -> std::string {
            if (args.size() < 4) return "-ERR wrong number of arguments for LRANGE\r\n";
            int start = static_cast<int>(std::stoll(args[2]));
            int stop  = static_cast<int>(std::stoll(args[3]));
            auto elems = store.lrange(args[1], start, stop);
            std::string out = "*" + std::to_string(elems.size()) + "\r\n";
            for (const auto& e : elems)
                out += "$" + std::to_string(e.size()) + "\r\n" + e + "\r\n";
            return out;
        }},
    };
}
