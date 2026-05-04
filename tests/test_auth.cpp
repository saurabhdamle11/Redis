#include "test_runner.h"
#include "commands/commands.h"
#include "resp/resp.h"
#include "store/store.h"
#include <string>
#include <vector>

// Minimal harness that replicates the auth gate in Server::on_readable.
struct AuthHarness {
    std::string password;
    bool authenticated;
    std::unordered_map<std::string, CommandHandler>& cmd;

    AuthHarness(const std::string& pw, std::unordered_map<std::string, CommandHandler>& c)
        : password(pw), authenticated(pw.empty()), cmd(c) {}

    std::string dispatch(const std::vector<std::string>& tokens) {
        if (tokens.empty()) return "";
        std::string name = tokens[0];
        for (char& c : name) c = static_cast<char>(toupper(c));

        if (name == "AUTH") {
            if (password.empty())
                return "-ERR Client sent AUTH, but no password is set.\r\n";
            if (tokens.size() < 2)
                return "-ERR wrong number of arguments for AUTH\r\n";
            if (tokens[1] == password) { authenticated = true;  return "+OK\r\n"; }
            authenticated = false;
            return "-ERR invalid username-password pair or user is disabled.\r\n";
        }

        if (!authenticated)
            return "-NOAUTH Authentication required.\r\n";

        auto it = cmd.find(name);
        return it != cmd.end() ? it->second(tokens) : "-ERR unknown command\r\n";
    }
};

// No password set — every connection is immediately authenticated.
void test_no_password_open_access(std::unordered_map<std::string, CommandHandler>& cmd) {
    AuthHarness h("", cmd);
    ASSERT_TRUE(h.authenticated);
    ASSERT_EQ(h.dispatch({"PING"}), "+PONG\r\n");
}

// No password set — AUTH returns a specific error.
void test_auth_when_no_password_configured(std::unordered_map<std::string, CommandHandler>& cmd) {
    AuthHarness h("", cmd);
    ASSERT_EQ(h.dispatch({"AUTH", "anything"}),
              "-ERR Client sent AUTH, but no password is set.\r\n");
}

// Correct password authenticates the connection.
void test_correct_password_authenticates(std::unordered_map<std::string, CommandHandler>& cmd) {
    AuthHarness h("secret", cmd);
    ASSERT_FALSE(h.authenticated);
    ASSERT_EQ(h.dispatch({"AUTH", "secret"}), "+OK\r\n");
    ASSERT_TRUE(h.authenticated);
}

// Wrong password leaves connection unauthenticated.
void test_wrong_password_rejected(std::unordered_map<std::string, CommandHandler>& cmd) {
    AuthHarness h("secret", cmd);
    ASSERT_EQ(h.dispatch({"AUTH", "wrong"}),
              "-ERR invalid username-password pair or user is disabled.\r\n");
    ASSERT_FALSE(h.authenticated);
}

// Commands before AUTH are blocked with NOAUTH.
void test_command_before_auth_blocked(std::unordered_map<std::string, CommandHandler>& cmd) {
    AuthHarness h("secret", cmd);
    ASSERT_EQ(h.dispatch({"PING"}), "-NOAUTH Authentication required.\r\n");
    ASSERT_EQ(h.dispatch({"SET",  "k", "v"}), "-NOAUTH Authentication required.\r\n");
    ASSERT_EQ(h.dispatch({"GET",  "k"}), "-NOAUTH Authentication required.\r\n");
}

// Commands work normally after AUTH.
void test_commands_work_after_auth(std::unordered_map<std::string, CommandHandler>& cmd) {
    AuthHarness h("secret", cmd);
    h.dispatch({"AUTH", "secret"});
    ASSERT_EQ(h.dispatch({"PING"}), "+PONG\r\n");
    h.dispatch({"SET", "k", "v"});
    ASSERT_EQ(h.dispatch({"GET", "k"}), "$1\r\nv\r\n");
}

// AUTH with missing password argument returns an error.
void test_auth_missing_argument(std::unordered_map<std::string, CommandHandler>& cmd) {
    AuthHarness h("secret", cmd);
    ASSERT_EQ(h.dispatch({"AUTH"}),
              "-ERR wrong number of arguments for AUTH\r\n");
}

// Re-authenticating with the correct password on an already-authenticated
// connection keeps the connection authenticated.
void test_reauth_correct(std::unordered_map<std::string, CommandHandler>& cmd) {
    AuthHarness h("secret", cmd);
    h.dispatch({"AUTH", "secret"});
    ASSERT_EQ(h.dispatch({"AUTH", "secret"}), "+OK\r\n");
    ASSERT_TRUE(h.authenticated);
}

// Re-authenticating with a wrong password revokes access immediately.
void test_reauth_wrong_revokes(std::unordered_map<std::string, CommandHandler>& cmd) {
    AuthHarness h("secret", cmd);
    h.dispatch({"AUTH", "secret"});
    ASSERT_TRUE(h.authenticated);
    h.dispatch({"AUTH", "wrong"});
    ASSERT_FALSE(h.authenticated);
    ASSERT_EQ(h.dispatch({"PING"}), "-NOAUTH Authentication required.\r\n");
}

int main() {
    Store store;
    auto cmd = build_command_table(store);

    test_no_password_open_access(cmd);
    test_auth_when_no_password_configured(cmd);
    test_correct_password_authenticates(cmd);
    test_wrong_password_rejected(cmd);
    test_command_before_auth_blocked(cmd);
    test_commands_work_after_auth(cmd);
    test_auth_missing_argument(cmd);
    test_reauth_correct(cmd);
    test_reauth_wrong_revokes(cmd);

    RUN_TESTS();
}
