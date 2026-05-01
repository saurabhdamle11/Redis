#pragma once
#include <chrono>
#include <condition_variable>
#include <mutex>
#include <optional>
#include <queue>
#include <string>
#include <unordered_map>
#include <vector>
#include <map>

class Store {

private:
    using TimePoint = std::chrono::steady_clock::time_point;

    struct Waiter {
        std::condition_variable cv;
        bool        ready = false;
        std::string value;       // pre-popped by wake_oldest_waiter
    };

    //Struct for streams
    struct StreamID{
        uint64_t ms = 0;
        uint64_t seq = 0;

        bool operator<(const StreamID&o) const{
            return ms<o.ms || (ms==o.ms && seq<o.seq);
        }

        bool operator==(const StreamID&o) const{
            return ms==o.ms && seq==o.seq;
        }
    };

    struct StreamEntry{
        StreamID id;
        std::vector<std::pair<std::string,std::string>> fields;
    };

    struct Stream{
        std::map<StreamID,StreamEntry> entries;
        uint64_t last_ms = 0;
        uint64_t last_seq = 0;
    };

    std::unordered_map<std::string, std::pair<std::string, std::optional<TimePoint>>> kv_;
    std::unordered_map<std::string, std::vector<std::string>> lists_;
    std::unordered_map<std::string, std::queue<Waiter*>> waiters_;
    std::mutex mtx_;

    std::unordered_map<std::string,Stream> streams_;

    void wake_oldest_waiter(const std::string& key);


public:
    void set(const std::string& key, const std::string& value,
             std::optional<std::chrono::milliseconds> ttl = {});
    std::optional<std::string> get(const std::string& key);

    int  rpush(const std::string& key, const std::vector<std::string>& values);
    int  lpush(const std::string& key, const std::vector<std::string>& values);
    int  llen(const std::string& key);
    std::vector<std::string> lrange(const std::string& key, int start, int stop);
    std::string type(const std::string& key);
    std::string blpop(const std::string& key, double timeout_sec);
    StreamID xadd(const std::string&key, const std::string&raw_id, const std::vector<std::pair<std::string,std::string>>&fields);
};
