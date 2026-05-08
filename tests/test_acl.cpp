#include "test_runner.h"
#include "acl/acl.h"
#include <cstdio>
#include <fstream>
#include <string>
#include <vector>

static bool set(Acl& a, const std::string& name, std::vector<std::string> rules) {
    std::string err;
    bool ok = a.set_user(name, rules, err);
    if (!ok) std::fprintf(stderr, "set_user(%s) failed: %s\n", name.c_str(), err.c_str());
    return ok;
}

void test_default_user_starts_open() {
    Acl a;
    ASSERT_TRUE(a.default_user_is_open());
    ASSERT_TRUE(a.user_enabled("default"));
    // nopass means any password is accepted.
    auto r = a.authenticate("default", "anything");
    ASSERT_TRUE(r.has_value());
    ASSERT_EQ(*r, "default");
    ASSERT_TRUE(a.command_allowed("default", "PING"));
    ASSERT_TRUE(a.keys_allowed("default", {"any-key"}));
}

void test_default_user_with_password() {
    Acl a;
    ASSERT_TRUE(set(a, "default", {"on", "resetpass", ">secret", "~*", "+@all"}));
    ASSERT_FALSE(a.default_user_is_open());

    ASSERT_FALSE(a.authenticate("default", "wrong").has_value());
    auto r = a.authenticate("default", "secret");
    ASSERT_TRUE(r.has_value());
    ASSERT_EQ(*r, "default");
}

void test_default_user_cannot_be_disabled() {
    Acl a;
    std::string err;
    ASSERT_FALSE(a.set_user("default", {"off"}, err));
    ASSERT_TRUE(a.user_enabled("default"));
}

void test_new_user_starts_off() {
    Acl a;
    ASSERT_TRUE(set(a, "alice", {">pw"}));
    // Without 'on', alice is disabled — auth must fail.
    ASSERT_FALSE(a.authenticate("alice", "pw").has_value());
}

void test_unknown_user_returns_nullopt() {
    Acl a;
    ASSERT_FALSE(a.authenticate("ghost", "anything").has_value());
}

void test_command_allow_deny() {
    Acl a;
    ASSERT_TRUE(set(a, "alice", {"on", ">pw", "+@all", "-set"}));
    ASSERT_TRUE(a.command_allowed("alice", "GET"));
    ASSERT_FALSE(a.command_allowed("alice", "SET"));

    ASSERT_TRUE(set(a, "bob", {"on", ">pw", "+get", "+ping"}));
    ASSERT_TRUE(a.command_allowed("bob", "GET"));
    ASSERT_TRUE(a.command_allowed("bob", "PING"));
    ASSERT_FALSE(a.command_allowed("bob", "SET"));
}

void test_categories() {
    Acl a;
    ASSERT_TRUE(set(a, "reader", {"on", ">pw", "+@read", "~*"}));
    ASSERT_TRUE(a.command_allowed("reader", "GET"));
    ASSERT_TRUE(a.command_allowed("reader", "LRANGE"));
    ASSERT_TRUE(a.command_allowed("reader", "XREAD"));
    ASSERT_FALSE(a.command_allowed("reader", "SET"));
    ASSERT_FALSE(a.command_allowed("reader", "RPUSH"));
}

void test_keys_pattern() {
    Acl a;
    ASSERT_TRUE(set(a, "u", {"on", ">pw", "+@all", "~user:*", "~cache:?"}));
    ASSERT_TRUE(a.keys_allowed("u", {"user:1"}));
    ASSERT_TRUE(a.keys_allowed("u", {"cache:a"}));
    ASSERT_FALSE(a.keys_allowed("u", {"other"}));
    ASSERT_FALSE(a.keys_allowed("u", {"cache:ab"}));
    // Mixed: any disallowed key fails the whole command.
    ASSERT_FALSE(a.keys_allowed("u", {"user:1", "other"}));
}

void test_allkeys_then_resetkeys() {
    Acl a;
    ASSERT_TRUE(set(a, "u", {"on", ">pw", "+@all", "~*", "resetkeys", "~k:*"}));
    ASSERT_TRUE(a.keys_allowed("u", {"k:1"}));
    ASSERT_FALSE(a.keys_allowed("u", {"other"}));
}

void test_off_user_cannot_authenticate() {
    Acl a;
    ASSERT_TRUE(set(a, "u", {"on", ">pw", "+@all"}));
    ASSERT_TRUE(a.authenticate("u", "pw").has_value());
    ASSERT_TRUE(set(a, "u", {"off"}));
    ASSERT_FALSE(a.authenticate("u", "pw").has_value());
}

void test_nopass_clears_passwords() {
    Acl a;
    ASSERT_TRUE(set(a, "u", {"on", ">pw", "nopass", "+@all"}));
    auto u = a.get_user("u");
    ASSERT_TRUE(u.has_value());
    ASSERT_TRUE(u->nopass);
    ASSERT_TRUE(u->password_hashes.empty());
    // Any password works.
    ASSERT_TRUE(a.authenticate("u", "literally-anything").has_value());
}

void test_multiple_passwords() {
    Acl a;
    ASSERT_TRUE(set(a, "u", {"on", ">pw1", ">pw2", "+@all"}));
    ASSERT_TRUE(a.authenticate("u", "pw1").has_value());
    ASSERT_TRUE(a.authenticate("u", "pw2").has_value());
    ASSERT_FALSE(a.authenticate("u", "pw3").has_value());
}

void test_remove_password() {
    Acl a;
    ASSERT_TRUE(set(a, "u", {"on", ">pw1", ">pw2", "<pw1", "+@all"}));
    ASSERT_FALSE(a.authenticate("u", "pw1").has_value());
    ASSERT_TRUE(a.authenticate("u", "pw2").has_value());
}

void test_hash_form() {
    Acl a;
    std::string h = Acl::sha256_hex("pw");
    ASSERT_TRUE(set(a, "u", {"on", "#" + h, "+@all"}));
    ASSERT_TRUE(a.authenticate("u", "pw").has_value());
}

void test_deluser() {
    Acl a;
    ASSERT_TRUE(set(a, "u", {"on", ">pw", "+@all"}));
    ASSERT_TRUE(a.del_user("u"));
    ASSERT_FALSE(a.get_user("u").has_value());
    // Cannot delete default.
    ASSERT_FALSE(a.del_user("default"));
}

void test_pattern_matching() {
    ASSERT_TRUE(Acl::match_pattern("*",       "anything"));
    ASSERT_TRUE(Acl::match_pattern("a*",      "abc"));
    ASSERT_TRUE(Acl::match_pattern("*c",      "abc"));
    ASSERT_TRUE(Acl::match_pattern("a*c",     "abbbc"));
    ASSERT_TRUE(Acl::match_pattern("a?c",     "abc"));
    ASSERT_FALSE(Acl::match_pattern("a?c",    "ac"));
    ASSERT_FALSE(Acl::match_pattern("a*c",    "abcd"));
    ASSERT_TRUE(Acl::match_pattern("user:*",  "user:42"));
    ASSERT_FALSE(Acl::match_pattern("user:*", "users:42"));
}

void test_format_user_roundtrip() {
    Acl a;
    ASSERT_TRUE(set(a, "alice",
                    {"on", ">secret", "~user:*", "+@read", "+ping"}));
    auto u = a.get_user("alice");
    ASSERT_TRUE(u.has_value());
    std::string line = Acl::format_user(*u);
    // Spot-check the line format.
    ASSERT_TRUE(line.find("user alice on") != std::string::npos);
    ASSERT_TRUE(line.find("~user:*") != std::string::npos);
    ASSERT_TRUE(line.find("+ping") != std::string::npos);
}

void test_save_and_load() {
    std::string path = "/tmp/redis_acl_test_save.acl";
    std::remove(path.c_str());

    {
        Acl a;
        a.set_aclfile(path);
        ASSERT_TRUE(set(a, "alice", {"on", ">secret", "~user:*", "+@read", "-get"}));
        ASSERT_TRUE(set(a, "bob",   {"on", "nopass", "~*", "+@all"}));
        std::string err;
        ASSERT_TRUE(a.save_to_file(err));
    }

    Acl b;
    b.set_aclfile(path);
    std::string err;
    ASSERT_TRUE(b.load_from_file(err));

    auto alice = b.get_user("alice");
    ASSERT_TRUE(alice.has_value());
    ASSERT_TRUE(alice->enabled);
    ASSERT_FALSE(alice->nopass);
    ASSERT_FALSE(alice->allkeys);
    ASSERT_TRUE(b.authenticate("alice", "secret").has_value());
    ASSERT_TRUE(b.command_allowed("alice", "LRANGE"));
    ASSERT_FALSE(b.command_allowed("alice", "GET"));   // -get override

    auto bob = b.get_user("bob");
    ASSERT_TRUE(bob.has_value());
    ASSERT_TRUE(bob->nopass);
    ASSERT_TRUE(bob->allkeys);
    ASSERT_TRUE(bob->allcommands);

    std::remove(path.c_str());
}

void test_load_replaces_user_state() {
    // Write an aclfile that downgrades alice's permissions, then reload.
    std::string path = "/tmp/redis_acl_test_reload.acl";
    {
        std::ofstream f(path, std::ios::trunc);
        f << "user default on nopass ~* +@all\n";
        f << "user alice on >pw ~* +get\n";
    }

    Acl a;
    a.set_aclfile(path);
    std::string err;
    ASSERT_TRUE(a.load_from_file(err));
    ASSERT_TRUE(a.command_allowed("alice", "GET"));
    ASSERT_FALSE(a.command_allowed("alice", "SET"));

    // Rewrite the file — alice now has more permissions. Reload must apply.
    {
        std::ofstream f(path, std::ios::trunc);
        f << "user default on nopass ~* +@all\n";
        f << "user alice on >pw ~* +@all\n";
    }
    ASSERT_TRUE(a.load_from_file(err));
    ASSERT_TRUE(a.command_allowed("alice", "SET"));

    std::remove(path.c_str());
}

void test_unknown_rule_rejected() {
    Acl a;
    std::string err;
    ASSERT_FALSE(a.set_user("u", {"on", "definitely-not-a-rule"}, err));
    ASSERT_TRUE(err.find("unknown ACL rule") != std::string::npos);
}

void test_unknown_category_rejected() {
    Acl a;
    std::string err;
    ASSERT_FALSE(a.set_user("u", {"on", "+@frobozz"}, err));
    ASSERT_TRUE(err.find("unknown category") != std::string::npos);
}

int main() {
    test_default_user_starts_open();
    test_default_user_with_password();
    test_default_user_cannot_be_disabled();
    test_new_user_starts_off();
    test_unknown_user_returns_nullopt();
    test_command_allow_deny();
    test_categories();
    test_keys_pattern();
    test_allkeys_then_resetkeys();
    test_off_user_cannot_authenticate();
    test_nopass_clears_passwords();
    test_multiple_passwords();
    test_remove_password();
    test_hash_form();
    test_deluser();
    test_pattern_matching();
    test_format_user_roundtrip();
    test_save_and_load();
    test_load_replaces_user_state();
    test_unknown_rule_rejected();
    test_unknown_category_rejected();
    RUN_TESTS();
}
