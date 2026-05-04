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

        {"TYPE", [&store](const Args& args) -> std::string{
            if(args.size()<2) {return "-ERR wrong number of arguments for TYPE\r\n";}
            return "+" + store.type(args[1])+"\r\n";
        }},

        {"BLPOP", [&store](const Args&args) -> std::string{
            if(args.size()<3) {return "-ERR wrong number of arguments for BLPOP\r\n";}
            return store.blpop(args[1], std::stod(args[2]));
        }},

        {"XADD", [&store](const Args&args) -> std::string{
            if(args.size() <5 || (args.size()-3) %2 != 0){
                return "-ERR wrong number of arguments for XADD\r\n";
            }

            if(args[2] == "0-0"){
                return "-ERR The ID specified in XADD must be greater than 0-0\r\n";
            }

            std::vector<std::pair<std::string,std::string>> fields;

            for(size_t i=3; i<args.size(); i+=2){
                fields.emplace_back(args[i], args[i + 1]);
            }

            auto id = store.xadd(args[1],args[2],fields);

            if(id.ms==0 && id.seq==0){
                return "-ERR The ID specified in XADD is equal or smaller than the target stream top item\r\n";
            }

            std::string id_str = std::to_string(id.ms) + "-" + std::to_string(id.seq);
            return "$" + std::to_string(id_str.size()) + "\r\n" + id_str + "\r\n";
        }}
    };
}
