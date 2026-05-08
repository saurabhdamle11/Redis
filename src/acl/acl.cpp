#include "acl.h"

#include <openssl/evp.h>

#include <algorithm>
#include <cctype>
#include <fstream>
#include <map>
#include <sstream>

namespace {

// Categories. Lowercase command names. Keep in sync with commands.cpp.
const std::map<std::string, std::set<std::string>>& categories() {
    static const std::map<std::string, std::set<std::string>> cats = {
        {"all",        {"ping","echo","auth","get","set","rpush","lpush","llen",
                        "lrange","type","blpop","xadd","xread","xrange","acl"}},
        {"read",       {"get","llen","lrange","type","xread","xrange"}},
        {"write",      {"set","rpush","lpush","blpop","xadd"}},
        {"string",     {"get","set"}},
        {"list",       {"rpush","lpush","llen","lrange","blpop"}},
        {"stream",     {"xadd","xread","xrange"}},
        {"connection", {"ping","echo","auth"}},
        {"admin",      {"acl"}},
        {"dangerous",  {"acl"}},
        {"fast",       {"ping","echo","type","llen","get","set","auth"}},
        {"slow",       {"lrange","blpop","xread","xrange","xadd","rpush","lpush"}},
        {"keyspace",   {"get","set","type"}},
    };
    return cats;
}

std::vector<std::string> tokenize_rules(const std::string& s) {
    std::vector<std::string> out;
    std::istringstream iss(s);
    std::string tok;
    while (iss >> tok) out.push_back(tok);
    return out;
}

}  // namespace

std::string Acl::lower(std::string s) {
    for (char& c : s) c = static_cast<char>(std::tolower(static_cast<unsigned char>(c)));
    return s;
}

std::string Acl::sha256_hex(const std::string& s) {
    unsigned char digest[EVP_MAX_MD_SIZE];
    unsigned int  dlen = 0;
    EVP_MD_CTX* ctx = EVP_MD_CTX_new();
    EVP_DigestInit_ex(ctx, EVP_sha256(), nullptr);
    EVP_DigestUpdate(ctx, s.data(), s.size());
    EVP_DigestFinal_ex(ctx, digest, &dlen);
    EVP_MD_CTX_free(ctx);

    static const char* hex = "0123456789abcdef";
    std::string out;
    out.reserve(dlen * 2);
    for (unsigned int i = 0; i < dlen; ++i) {
        out += hex[(digest[i] >> 4) & 0xf];
        out += hex[digest[i]        & 0xf];
    }
    return out;
}

bool Acl::match_pattern(const std::string& pat, const std::string& s) {
    size_t i = 0, j = 0;
    size_t star = std::string::npos, match = 0;
    while (j < s.size()) {
        if (i < pat.size() && (pat[i] == '?' || pat[i] == s[j])) {
            ++i; ++j;
        } else if (i < pat.size() && pat[i] == '*') {
            star = i++;
            match = j;
        } else if (star != std::string::npos) {
            i = star + 1;
            j = ++match;
        } else {
            return false;
        }
    }
    while (i < pat.size() && pat[i] == '*') ++i;
    return i == pat.size();
}

Acl::Acl() {
    // Default user starts permissive (on, nopass, +@all, ~*) — matches Redis.
    User d;
    d.name = "default";
    d.enabled = true;
    d.nopass = true;
    d.allcommands = true;
    d.allkeys = true;
    users_["default"] = std::move(d);
}

Acl::User& Acl::ensure_user_locked(const std::string& name) {
    auto it = users_.find(name);
    if (it != users_.end()) return it->second;
    User u;
    u.name = name;
    u.enabled = false;          // freshly created users start off + nocommands + nokeys
    return users_.emplace(name, std::move(u)).first->second;
}

std::optional<std::string> Acl::authenticate(const std::string& username,
                                              const std::string& password) {
    std::lock_guard<std::mutex> lk(mtx_);
    auto it = users_.find(username);
    if (it == users_.end()) return std::nullopt;
    const User& u = it->second;
    if (!u.enabled) return std::nullopt;
    if (u.nopass) return u.name;

    std::string h = sha256_hex(password);
    for (const auto& want : u.password_hashes) {
        if (h == want) return u.name;
    }
    return std::nullopt;
}

bool Acl::user_enabled(const std::string& username) {
    std::lock_guard<std::mutex> lk(mtx_);
    auto it = users_.find(username);
    return it != users_.end() && it->second.enabled;
}

bool Acl::default_user_is_open() {
    std::lock_guard<std::mutex> lk(mtx_);
    auto it = users_.find("default");
    if (it == users_.end()) return false;
    const User& u = it->second;
    return u.enabled && u.nopass;
}

bool Acl::command_allowed(const std::string& username, const std::string& cmd_upper) {
    std::string cmd = lower(cmd_upper);
    std::lock_guard<std::mutex> lk(mtx_);
    auto it = users_.find(username);
    if (it == users_.end()) return false;
    const User& u = it->second;
    if (!u.enabled) return false;
    if (u.denied.count(cmd))  return false;
    if (u.allowed.count(cmd)) return true;
    return u.allcommands;
}

bool Acl::keys_allowed(const std::string& username,
                       const std::vector<std::string>& keys) {
    std::lock_guard<std::mutex> lk(mtx_);
    auto it = users_.find(username);
    if (it == users_.end()) return false;
    const User& u = it->second;
    if (u.allkeys) return true;
    for (const auto& k : keys) {
        bool ok = false;
        for (const auto& pat : u.key_patterns) {
            if (match_pattern(pat, k)) { ok = true; break; }
        }
        if (!ok) return false;
    }
    return true;
}

bool Acl::apply_rule_locked(User& u, const std::string& rule, std::string& err) {
    if (rule.empty()) return true;

    if (rule == "on")  { u.enabled = true;  return true; }
    if (rule == "off") {
        if (u.name == "default") { err = "default user cannot be disabled"; return false; }
        u.enabled = false; return true;
    }

    if (rule == "nopass") {
        u.nopass = true;
        u.password_hashes.clear();
        return true;
    }
    if (rule == "resetpass") {
        u.nopass = false;
        u.password_hashes.clear();
        return true;
    }

    if (!rule.empty() && rule[0] == '>') {
        std::string h = sha256_hex(rule.substr(1));
        if (std::find(u.password_hashes.begin(), u.password_hashes.end(), h)
                == u.password_hashes.end()) {
            u.password_hashes.push_back(h);
        }
        u.nopass = false;
        return true;
    }
    if (!rule.empty() && rule[0] == '<') {
        std::string h = sha256_hex(rule.substr(1));
        u.password_hashes.erase(
            std::remove(u.password_hashes.begin(), u.password_hashes.end(), h),
            u.password_hashes.end());
        return true;
    }
    if (!rule.empty() && rule[0] == '#') {
        std::string h = lower(rule.substr(1));
        if (h.size() != 64) { err = "invalid SHA-256 hash"; return false; }
        if (std::find(u.password_hashes.begin(), u.password_hashes.end(), h)
                == u.password_hashes.end()) {
            u.password_hashes.push_back(h);
        }
        u.nopass = false;
        return true;
    }
    if (!rule.empty() && rule[0] == '!') {
        std::string h = lower(rule.substr(1));
        u.password_hashes.erase(
            std::remove(u.password_hashes.begin(), u.password_hashes.end(), h),
            u.password_hashes.end());
        return true;
    }

    if (rule == "allkeys" || rule == "~*") {
        u.allkeys = true;
        u.key_patterns.clear();
        return true;
    }
    if (rule == "resetkeys") {
        u.allkeys = false;
        u.key_patterns.clear();
        return true;
    }
    if (!rule.empty() && rule[0] == '~') {
        std::string p = rule.substr(1);
        if (std::find(u.key_patterns.begin(), u.key_patterns.end(), p)
                == u.key_patterns.end()) {
            u.key_patterns.push_back(p);
        }
        u.allkeys = false;
        return true;
    }

    if (rule == "allcommands" || rule == "+@all") {
        u.allcommands = true;
        u.allowed.clear();
        u.denied.clear();
        return true;
    }
    if (rule == "nocommands" || rule == "-@all") {
        u.allcommands = false;
        u.allowed.clear();
        u.denied.clear();
        return true;
    }

    if (rule.size() >= 2 && rule[0] == '+' && rule[1] == '@') {
        std::string cat = lower(rule.substr(2));
        const auto& cats = categories();
        auto cit = cats.find(cat);
        if (cit == cats.end()) { err = "unknown category @" + cat; return false; }
        for (const auto& c : cit->second) {
            u.denied.erase(c);
            u.allowed.insert(c);
        }
        return true;
    }
    if (rule.size() >= 2 && rule[0] == '-' && rule[1] == '@') {
        std::string cat = lower(rule.substr(2));
        const auto& cats = categories();
        auto cit = cats.find(cat);
        if (cit == cats.end()) { err = "unknown category @" + cat; return false; }
        for (const auto& c : cit->second) {
            u.allowed.erase(c);
            u.denied.insert(c);
        }
        return true;
    }

    if (!rule.empty() && rule[0] == '+') {
        std::string c = lower(rule.substr(1));
        if (c.empty()) { err = "empty command name"; return false; }
        u.denied.erase(c);
        u.allowed.insert(c);
        return true;
    }
    if (!rule.empty() && rule[0] == '-') {
        std::string c = lower(rule.substr(1));
        if (c.empty()) { err = "empty command name"; return false; }
        u.allowed.erase(c);
        u.denied.insert(c);
        return true;
    }

    if (rule == "reset") {
        u.enabled = false;
        u.nopass = false;
        u.password_hashes.clear();
        u.allcommands = false;
        u.allowed.clear();
        u.denied.clear();
        u.allkeys = false;
        u.key_patterns.clear();
        return true;
    }

    err = "unknown ACL rule '" + rule + "'";
    return false;
}

bool Acl::set_user(const std::string& name,
                   const std::vector<std::string>& rules,
                   std::string& err) {
    if (name.empty()) { err = "username is empty"; return false; }
    std::lock_guard<std::mutex> lk(mtx_);
    User& u = ensure_user_locked(name);
    for (const auto& r : rules) {
        if (!apply_rule_locked(u, r, err)) return false;
    }
    return true;
}

bool Acl::del_user(const std::string& name) {
    if (name == "default") return false;
    std::lock_guard<std::mutex> lk(mtx_);
    return users_.erase(name) > 0;
}

std::vector<std::string> Acl::list_users() {
    std::lock_guard<std::mutex> lk(mtx_);
    std::vector<std::string> out;
    out.reserve(users_.size());
    for (const auto& [k, _] : users_) out.push_back(k);
    std::sort(out.begin(), out.end());
    return out;
}

std::optional<Acl::User> Acl::get_user(const std::string& name) {
    std::lock_guard<std::mutex> lk(mtx_);
    auto it = users_.find(name);
    if (it == users_.end()) return std::nullopt;
    return it->second;
}

std::string Acl::format_user(const User& u) {
    std::ostringstream os;
    os << "user " << u.name << " " << (u.enabled ? "on" : "off");

    if (u.nopass) {
        os << " nopass";
    } else {
        for (const auto& h : u.password_hashes) os << " #" << h;
    }

    if (u.allkeys) {
        os << " ~*";
    } else {
        for (const auto& p : u.key_patterns) os << " ~" << p;
    }

    if (u.allcommands) os << " +@all";
    else               os << " -@all";
    for (const auto& c : u.allowed) os << " +" << c;
    for (const auto& c : u.denied)  os << " -" << c;

    return os.str();
}

void Acl::set_aclfile(const std::string& path) {
    std::lock_guard<std::mutex> lk(mtx_);
    aclfile_ = path;
}

std::string Acl::aclfile() {
    std::lock_guard<std::mutex> lk(mtx_);
    return aclfile_;
}

bool Acl::load_from_file(std::string& err) {
    std::string path;
    {
        std::lock_guard<std::mutex> lk(mtx_);
        path = aclfile_;
    }
    if (path.empty()) { err = "no aclfile configured"; return false; }

    std::ifstream in(path);
    if (!in.is_open()) { err = "cannot open aclfile: " + path; return false; }

    std::string line;
    int lineno = 0;
    while (std::getline(in, line)) {
        ++lineno;
        // Skip blank lines and comments.
        size_t first = line.find_first_not_of(" \t\r\n");
        if (first == std::string::npos) continue;
        if (line[first] == '#') continue;

        auto toks = tokenize_rules(line);
        if (toks.size() < 2 || toks[0] != "user") {
            err = "aclfile line " + std::to_string(lineno) + ": expected 'user <name> ...'";
            return false;
        }
        std::string name = toks[1];

        // A single line is the full state of that user. Reset before applying.
        {
            std::lock_guard<std::mutex> lk(mtx_);
            User& u = ensure_user_locked(name);
            std::string ignored;
            apply_rule_locked(u, "reset", ignored);
        }

        std::vector<std::string> rules(toks.begin() + 2, toks.end());
        std::string serr;
        if (!set_user(name, rules, serr)) {
            err = "aclfile line " + std::to_string(lineno) + ": " + serr;
            return false;
        }
    }
    return true;
}

bool Acl::save_to_file(std::string& err) {
    std::string path;
    std::vector<User> snapshot;
    {
        std::lock_guard<std::mutex> lk(mtx_);
        path = aclfile_;
        snapshot.reserve(users_.size());
        for (const auto& [_, u] : users_) snapshot.push_back(u);
    }
    if (path.empty()) { err = "no aclfile configured"; return false; }

    std::sort(snapshot.begin(), snapshot.end(),
              [](const User& a, const User& b) { return a.name < b.name; });

    // Write to a temp file then rename — atomic on POSIX.
    std::string tmp = path + ".tmp";
    {
        std::ofstream out(tmp, std::ios::trunc);
        if (!out.is_open()) { err = "cannot open " + tmp + " for write"; return false; }
        for (const auto& u : snapshot) out << format_user(u) << "\n";
        if (!out.good()) { err = "write failed"; return false; }
    }
    if (std::rename(tmp.c_str(), path.c_str()) != 0) {
        err = "rename failed";
        return false;
    }
    return true;
}

const std::set<std::string>& Acl::commands_in_category(const std::string& cat) {
    static const std::set<std::string> empty;
    const auto& cats = categories();
    auto it = cats.find(cat);
    return it == cats.end() ? empty : it->second;
}

std::vector<std::string> Acl::all_categories() {
    std::vector<std::string> out;
    for (const auto& [k, _] : categories()) out.push_back(k);
    std::sort(out.begin(), out.end());
    return out;
}
