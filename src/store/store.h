#pragma once
#include <chrono>
#include <mutex>
#include <optional>
#include <string>
#include <unordered_map>
#include <vector>

class Store {
public:
    void set(const std::string& key, const std::string& value,
             std::optional<std::chrono::milliseconds> ttl = {});
    std::optional<std::string> get(const std::string& key);

    int  rpush(const std::string& key, const std::vector<std::string>& values);
    int  lpush(const std::string& key, const std::vector<std::string>& values);
    int  llen(const std::string& key);
    std::vector<std::string> lrange(const std::string& key, int start, int stop);

private:
    using TimePoint = std::chrono::steady_clock::time_point;
    std::unordered_map<std::string, std::pair<std::string, std::optional<TimePoint>>> kv_;
    std::unordered_map<std::string, std::vector<std::string>> lists_;
    std::mutex mtx_;
};
