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

void Store::wake_oldest_waiter(const std::string& key) {
    auto wit = waiters_.find(key);
    if (wit == waiters_.end() || wit->second.empty()) return;
    auto lit = lists_.find(key);
    if (lit == lists_.end() || lit->second.empty()) return;

    // Pre-pop the element for the waiter so no other thread can steal it.
    Waiter* w = wit->second.front();
    wit->second.pop();
    w->value = lit->second.front();
    lit->second.erase(lit->second.begin());
    if (lit->second.empty()) lists_.erase(lit);

    w->ready = true;
    w->cv.notify_one();
}

int Store::rpush(const std::string& key, const std::vector<std::string>& values) {
    std::lock_guard<std::mutex> lk(mtx_);
    auto& list = lists_[key];
    for (const auto& v : values) list.push_back(v);
    wake_oldest_waiter(key);
    return static_cast<int>(list.size());
}

int Store::lpush(const std::string& key, const std::vector<std::string>& values) {
    std::lock_guard<std::mutex> lk(mtx_);
    auto& list = lists_[key];
    for (const auto& v : values) list.insert(list.begin(), v);
    wake_oldest_waiter(key);
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

std::string Store::type(const std::string &key){
    std::lock_guard<std::mutex> lk(mtx_);
    if(kv_.count(key)){return "string";}
    if(lists_.count(key)){return "list";}
    if(streams_.count(key)){return "stream";}
    return "none";
}

std::string Store::blpop(const std::string& key, double timeout_sec) {
    std::unique_lock<std::mutex> lk(mtx_);

    // Fast path: data available AND no one has been waiting longer.
    auto& wq = waiters_[key];
    auto lit = lists_.find(key);
    if (wq.empty() && lit != lists_.end() && !lit->second.empty()) {
        std::string val = lit->second.front();
        lit->second.erase(lit->second.begin());
        if (lit->second.empty()) lists_.erase(lit);
        return "*2\r\n$" + std::to_string(key.size()) + "\r\n" + key + "\r\n"
             + "$" + std::to_string(val.size()) + "\r\n" + val + "\r\n";
    }

    // Register at the back of the per-key queue (FIFO = longest waiter at front).
    Waiter w;
    wq.push(&w);

    bool got_it = false;
    if (timeout_sec == 0) {
        w.cv.wait(lk, [&]{ return w.ready; });
        got_it = true;
    } else {
        auto dur = std::chrono::duration_cast<std::chrono::milliseconds>(
            std::chrono::duration<double>(timeout_sec));
        got_it = w.cv.wait_for(lk, dur, [&]{ return w.ready; });
    }

    if (!got_it) {
        // Timed out -- remove ourselves so we are never handed a value later.
        auto& q = waiters_[key];
        std::queue<Waiter*> cleaned;
        while (!q.empty()) {
            if (q.front() != &w) cleaned.push(q.front());
            q.pop();
        }
        waiters_[key] = std::move(cleaned);
        return "*-1\r\n";
    }

    // Value was pre-popped by wake_oldest_waiter; use it directly.
    return "*2\r\n$" + std::to_string(key.size()) + "\r\n" + key + "\r\n"
         + "$" + std::to_string(w.value.size()) + "\r\n" + w.value + "\r\n";
}

Store::StreamID Store::xadd(const std::string&key, const std::string&raw_id, const std::vector<std::pair<std::string, std::string>>&fields){
    std::lock_guard<std::mutex> lk(mtx_);

    auto& stream = streams_[key];

    uint64_t now_ms = static_cast<uint64_t>(
        std::chrono::duration_cast<std::chrono::milliseconds>(
            std::chrono::system_clock::now().time_since_epoch()
        ).count()
    );

    StreamID id;

    if(raw_id == "*"){
        id.ms = std::max(now_ms,stream.last_ms);
        id.seq = (id.ms==stream.last_ms)? stream.last_seq+1:0;
    }
    else{
        auto dash = raw_id.find('-');
        id.ms = std::stoull(raw_id.substr(0,dash));
        std::string seq_part = raw_id.substr(dash+1);
        if(seq_part == "*"){
            id.seq = (id.ms == stream.last_ms) ? stream.last_seq + 1: 0;
        }
        else{
            id.seq = std::stoull(seq_part);
        }
    }

    if(id.ms < stream.last_ms || (id.ms == stream.last_ms && id.seq<= stream.last_seq)){
        return {0,0};
    }

    StreamEntry entry{id,fields};
    stream.entries.emplace_hint(stream.entries.end(),id,entry);
    stream.last_ms = id.ms;
    stream.last_seq = id.seq;

    return id;
}