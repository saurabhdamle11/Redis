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

        {"XREAD", [&store](const Args& args) -> std::string {
            // XREAD STREAMS key1 [key2...] id1 [id2...]
            // args[1] must be STREAMS; remaining args split evenly into keys and ids
            if (args.size() < 4) return "-ERR wrong number of arguments for XREAD\r\n";
            std::string streams_kw = args[1];
            for (char& c : streams_kw) c = static_cast<char>(toupper(c));
            if (streams_kw != "STREAMS") return "-ERR syntax error\r\n";

            size_t n = (args.size() - 2) / 2;
            if ((args.size() - 2) % 2 != 0) return "-ERR syntax error\r\n";

            auto parse_id = [](const std::string& s, uint64_t& ms, uint64_t& seq) {
                auto dash = s.find('-');
                if (dash == std::string::npos) {
                    ms  = std::stoull(s);
                    seq = 0;
                } else {
                    ms  = std::stoull(s.substr(0, dash));
                    seq = std::stoull(s.substr(dash + 1));
                }
            };

            std::string out = "*" + std::to_string(n) + "\r\n";
            for (size_t i = 0; i < n; ++i) {
                const std::string& key = args[2 + i];
                uint64_t ms, seq;
                parse_id(args[2 + n + i], ms, seq);

                auto entries = store.xread(key, ms, seq);

                out += "*2\r\n";
                out += "$" + std::to_string(key.size()) + "\r\n" + key + "\r\n";
                out += "*" + std::to_string(entries.size()) + "\r\n";
                for (const auto& entry : entries) {
                    out += "*2\r\n";
                    out += "$" + std::to_string(entry.id.size()) + "\r\n" + entry.id + "\r\n";
                    out += "*" + std::to_string(entry.fields.size() * 2) + "\r\n";
                    for (const auto& [k, v] : entry.fields) {
                        out += "$" + std::to_string(k.size()) + "\r\n" + k + "\r\n";
                        out += "$" + std::to_string(v.size()) + "\r\n" + v + "\r\n";
                    }
                }
            }
            return out;
        }},

        {"XRANGE", [&store](const Args& args) -> std::string {
            if (args.size() < 4) return "-ERR wrong number of arguments for XRANGE\r\n";

            auto parse_id = [](const std::string& s, bool is_start,
                               uint64_t& ms, uint64_t& seq) {
                if (s == "-") { ms = 0; seq = 0; return; }
                if (s == "+") { ms = UINT64_MAX; seq = UINT64_MAX; return; }
                auto dash = s.find('-');
                if (dash == std::string::npos) {
                    ms  = std::stoull(s);
                    seq = is_start ? 0 : UINT64_MAX;
                } else {
                    ms  = std::stoull(s.substr(0, dash));
                    seq = std::stoull(s.substr(dash + 1));
                }
            };

            uint64_t start_ms, start_seq, end_ms, end_seq;
            parse_id(args[2], true,  start_ms, start_seq);
            parse_id(args[3], false, end_ms,   end_seq);

            auto entries = store.xrange(args[1], start_ms, start_seq, end_ms, end_seq);

            std::string out = "*" + std::to_string(entries.size()) + "\r\n";
            for (const auto& entry : entries) {
                out += "*2\r\n";
                out += "$" + std::to_string(entry.id.size()) + "\r\n" + entry.id + "\r\n";
                out += "*" + std::to_string(entry.fields.size() * 2) + "\r\n";
                for (const auto& [k, v] : entry.fields) {
                    out += "$" + std::to_string(k.size()) + "\r\n" + k + "\r\n";
                    out += "$" + std::to_string(v.size()) + "\r\n" + v + "\r\n";
                }
            }
            return out;
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
