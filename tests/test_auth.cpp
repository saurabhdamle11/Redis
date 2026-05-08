#include "test_runner.h"
#include "acl/acl.h"
#include "commands/commands.h"
#include "store/store.h"
#include <string>
#include <vector>

// Replicates the AUTH-and-dispatch path in Server::on_readable, but without
// touching sockets so the gate logic can be exercised in isolation.
struct Harness {
    Acl& acl;
    bool authenticated;
    std::string username = "default";
    std::unordered_map<std::string, CommandHandler>& cmd;

    Harness(Acl& a, std::unordered_map<std::string, CommandHandler>& c)
        : acl(a), authenticated(a.default_user_is_open()), cmd(c) {}

    std::string dispatch(const std::vector<std::string>& tokens) {
        if (tokens.empty()) return "";
        std::string name = tokens[0];
        for (char& c : name) c = static_cast<char>(toupper(c));

        if (name == "AUTH") {
            std::string user, pass;
            if (tokens.size() == 2)      { user = "default"; pass = tokens[1]; }
            else if (tokens.size() == 3) { user = tokens[1]; pass = tokens[2]; }
            else return "-ERR wrong number of arguments for AUTH\r\n";

            auto r = acl.authenticate(user, pass);
            if (!r) {
                authenticated = false;
                return "-WRONGPASS invalid username-password pair or user is disabled.\r\n";
            }
            username = *r;
            authenticated = true;
            return "+OK\r\n";
        }

        if (!acl.user_enabled(username)) authenticated = false;
        if (!authenticated) return "-NOAUTH Authentication required.\r\n";

        if (!acl.command_allowed(username, name))
            return "-NOPERM denied\r\n";

        auto it = cmd.find(name);
        return it != cmd.end() ? it->second(tokens) : "-ERR unknown command\r\n";
    }
};

// Default user (open: on + nopass) — every connection is authenticated immediately.
void test_default_user_open(std::unordered_map<std::string, CommandHandler>& cmd) {
    Acl acl;
    Harness h(acl, cmd);
    ASSERT_TRUE(h.authenticated);
    ASSERT_EQ(h.dispatch({"PING"}), "+PONG\r\n");
}

// Once the default user has a password, fresh connections are unauthenticated.
void test_default_user_password(std::unordered_map<std::string, CommandHandler>& cmd) {
    Acl acl;
    std::string err;
    acl.set_user("default", {"on", "resetpass", ">secret", "~*", "+@all"}, err);

    Harness h(acl, cmd);
    ASSERT_FALSE(h.authenticated);
    ASSERT_EQ(h.dispatch({"PING"}), "-NOAUTH Authentication required.\r\n");
    ASSERT_EQ(h.dispatch({"AUTH", "secret"}), "+OK\r\n");
    ASSERT_TRUE(h.authenticated);
    ASSERT_EQ(h.dispatch({"PING"}), "+PONG\r\n");
}

void test_wrong_password_rejected(std::unordered_map<std::string, CommandHandler>& cmd) {
    Acl acl;
    std::string err;
    acl.set_user("default", {"on", "resetpass", ">secret", "~*", "+@all"}, err);

    Harness h(acl, cmd);
    ASSERT_EQ(h.dispatch({"AUTH", "wrong"}),
              "-WRONGPASS invalid username-password pair or user is disabled.\r\n");
    ASSERT_FALSE(h.authenticated);
}

void test_two_form_auth(std::unordered_map<std::string, CommandHandler>& cmd) {
    Acl acl;
    std::string err;
    // Lock down the default user so connections start unauthenticated.
    acl.set_user("default", {"on", "resetpass", ">d-pw", "~*", "+@all"}, err);
    acl.set_user("alice",   {"on", ">pw", "+@all", "~*"}, err);

    Harness h(acl, cmd);
    ASSERT_FALSE(h.authenticated);
    ASSERT_EQ(h.dispatch({"AUTH", "alice", "wrong"}),
              "-WRONGPASS invalid username-password pair or user is disabled.\r\n");
    ASSERT_EQ(h.dispatch({"AUTH", "alice", "pw"}), "+OK\r\n");
    ASSERT_EQ(h.username, "alice");
}

void test_unknown_user_returns_wrongpass(std::unordered_map<std::string, CommandHandler>& cmd) {
    Acl acl;
    Harness h(acl, cmd);
    ASSERT_EQ(h.dispatch({"AUTH", "ghost", "anything"}),
              "-WRONGPASS invalid username-password pair or user is disabled.\r\n");
}

void test_auth_missing_argument(std::unordered_map<std::string, CommandHandler>& cmd) {
    Acl acl;
    Harness h(acl, cmd);
    ASSERT_EQ(h.dispatch({"AUTH"}),
              "-ERR wrong number of arguments for AUTH\r\n");
}

// Re-AUTH with a wrong password drops the connection back to unauthenticated.
void test_reauth_wrong_revokes(std::unordered_map<std::string, CommandHandler>& cmd) {
    Acl acl;
    std::string err;
    acl.set_user("default", {"on", "resetpass", ">secret", "~*", "+@all"}, err);

    Harness h(acl, cmd);
    h.dispatch({"AUTH", "secret"});
    ASSERT_TRUE(h.authenticated);
    h.dispatch({"AUTH", "wrong"});
    ASSERT_FALSE(h.authenticated);
    ASSERT_EQ(h.dispatch({"PING"}), "-NOAUTH Authentication required.\r\n");
}

// A user disabled mid-session is locked out on the next dispatch.
void test_user_disabled_mid_session(std::unordered_map<std::string, CommandHandler>& cmd) {
    Acl acl;
    std::string err;
    acl.set_user("alice", {"on", ">pw", "+@all", "~*"}, err);

    Harness h(acl, cmd);
    h.dispatch({"AUTH", "alice", "pw"});
    ASSERT_TRUE(h.authenticated);

    acl.set_user("alice", {"off"}, err);
    ASSERT_EQ(h.dispatch({"PING"}), "-NOAUTH Authentication required.\r\n");
}

// NOPERM kicks in for commands the user is not allowed to run.
void test_noperm_for_denied_command(std::unordered_map<std::string, CommandHandler>& cmd) {
    Acl acl;
    std::string err;
    acl.set_user("reader", {"on", ">pw", "+@read", "~*"}, err);

    Harness h(acl, cmd);
    h.dispatch({"AUTH", "reader", "pw"});
    ASSERT_TRUE(h.authenticated);
    ASSERT_EQ(h.dispatch({"SET", "k", "v"}), "-NOPERM denied\r\n");
}

int main() {
    Store store;
    auto cmd = build_command_table(store);

    test_default_user_open(cmd);
    test_default_user_password(cmd);
    test_wrong_password_rejected(cmd);
    test_two_form_auth(cmd);
    test_unknown_user_returns_wrongpass(cmd);
    test_auth_missing_argument(cmd);
    test_reauth_wrong_revokes(cmd);
    test_user_disabled_mid_session(cmd);
    test_noperm_for_denied_command(cmd);

    RUN_TESTS();
}
