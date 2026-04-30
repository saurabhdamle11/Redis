#include "store.h"

void Store::set(const std::string& key, const std::string& value,
                std::optional<std::chrono::milliseconds> ttl) {
    std::lock_guard<std::mutex> lk(mtx_);
    std::optional<TimePoint> expiry;
    if (ttl) expiry = std::chrono::steady_clock::now() + *ttl;
    kv_[key] = {value, expiry};
}

std::optional<std::string> Store::get(const std::string& key) {
    std::lock_guard<std::mutex> lk(mtx_);
    auto it = kv_.find(key);
    if (it == kv_.end()) return std::nullopt;
    auto& [val, expiry] = it->second;
    if (expiry && std::chrono::steady_clock::now() > *expiry) {
        kv_.erase(it);
        return std::nullopt;
    }
    return val;
}

int Store::rpush(const std::string& key, const std::vector<std::string>& values) {
    std::lock_guard<std::mutex> lk(mtx_);
    auto& list = lists_[key];
    for (const auto& v : values) list.push_back(v);
    return static_cast<int>(list.size());
}

int Store::lpush(const std::string& key, const std::vector<std::string>& values) {
    std::lock_guard<std::mutex> lk(mtx_);
    auto& list = lists_[key];
    for (const auto& v : values) list.insert(list.begin(), v);
    return static_cast<int>(list.size());
}

int Store::llen(const std::string& key) {
    std::lock_guard<std::mutex> lk(mtx_);
    auto it = lists_.find(key);
    return it == lists_.end() ? 0 : static_cast<int>(it->second.size());
}

std::vector<std::string> Store::lrange(const std::string& key, int start, int stop) {
    std::lock_guard<std::mutex> lk(mtx_);
    auto it = lists_.find(key);
    if (it == lists_.end()) return {};
    const auto& list = it->second;
    int n = static_cast<int>(list.size());
    if (start < 0) start = std::max(0, n + start);
    if (stop  < 0) stop  = n + stop;
    if (stop >= n) stop  = n - 1;
    if (start > stop) return {};
    return {list.begin() + start, list.begin() + stop + 1};
}
