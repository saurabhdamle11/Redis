#pragma once
#include <mutex>
#include <optional>
#include <set>
#include <string>
#include <unordered_map>
#include <vector>

class Acl {
public:
    struct User {
        std::string name;
        bool enabled = true;
        bool nopass = false;

        // SHA-256 hex digests (lowercase) of accepted passwords.
        std::vector<std::string> password_hashes;

        // Baseline command policy: when true, every command is allowed unless
        // it appears in `denied`; when false, only commands in `allowed` pass.
        bool allcommands = false;
        std::set<std::string> allowed;
        std::set<std::string> denied;

        // Baseline key policy: when true, every key matches; when false, the
        // key must match at least one pattern.
        bool allkeys = false;
        std::vector<std::string> key_patterns;
    };

    Acl();

    // Returns the canonical username on success, std::nullopt on failure.
    // Uses SHA-256 + plaintext comparison; nopass users accept any password.
    std::optional<std::string> authenticate(const std::string& username,
                                             const std::string& password);

    bool user_enabled(const std::string& username);
    bool default_user_is_open();   // default user: on + nopass

    bool command_allowed(const std::string& username, const std::string& cmd_upper);
    bool keys_allowed(const std::string& username,
                      const std::vector<std::string>& keys);

    // Returns false and fills `err` on rule parsing failure. Creates the user
    // if missing. Cannot disable or delete the default user.
    bool set_user(const std::string& name,
                  const std::vector<std::string>& rules,
                  std::string& err);
    bool del_user(const std::string& name);
    std::vector<std::string> list_users();
    std::optional<User> get_user(const std::string& name);

    // Render a single user as an ACL-file / ACL LIST line.
    static std::string format_user(const User& u);

    // Persistence
    void set_aclfile(const std::string& path);
    std::string aclfile();
    bool load_from_file(std::string& err);
    bool save_to_file(std::string& err);

    // Categories
    static const std::set<std::string>& commands_in_category(const std::string& cat);
    static std::vector<std::string> all_categories();

    // SHA-256 of `s`, lowercase hex.
    static std::string sha256_hex(const std::string& s);

    // Glob match: supports `*` and `?`.
    static bool match_pattern(const std::string& pattern, const std::string& s);

private:
    std::mutex mtx_;
    std::unordered_map<std::string, User> users_;
    std::string aclfile_;

    // All locked helpers assume the caller holds mtx_.
    User& ensure_user_locked(const std::string& name);
    bool apply_rule_locked(User& u, const std::string& rule, std::string& err);
    static std::string lower(std::string s);
};
