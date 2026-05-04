#include "test_runner.h"
#include "commands/commands.h"
#include "store/store.h"
#include <thread>

// Basic read: entry after 0-0 returns the only entry present.
void test_xread_basic(std::unordered_map<std::string, CommandHandler>& cmd) {
    cmd["XADD"]({"XADD", "x1", "0-1", "temperature", "96"});
    ASSERT_EQ(cmd["XREAD"]({"XREAD", "STREAMS", "x1", "0-0"}),
              "*1\r\n"
              "*2\r\n$2\r\nx1\r\n"
              "*1\r\n"
              "*2\r\n$3\r\n0-1\r\n"
              "*2\r\n$11\r\ntemperature\r\n$2\r\n96\r\n");
}

// XREAD is exclusive: the entry matching the given ID is not returned.
void test_xread_exclusive(std::unordered_map<std::string, CommandHandler>& cmd) {
    cmd["XADD"]({"XADD", "x2", "1-0", "f", "a"});
    cmd["XADD"]({"XADD", "x2", "2-0", "f", "b"});
    // reading after 1-0 must return only 2-0
    ASSERT_EQ(cmd["XREAD"]({"XREAD", "STREAMS", "x2", "1-0"}),
              "*1\r\n"
              "*2\r\n$2\r\nx2\r\n"
              "*1\r\n"
              "*2\r\n$3\r\n2-0\r\n"
              "*2\r\n$1\r\nf\r\n$1\r\nb\r\n");
}

// Reading after the last entry returns an empty entries array.
void test_xread_no_new_entries(std::unordered_map<std::string, CommandHandler>& cmd) {
    cmd["XADD"]({"XADD", "x3", "5-0", "f", "v"});
    ASSERT_EQ(cmd["XREAD"]({"XREAD", "STREAMS", "x3", "5-0"}),
              "*1\r\n"
              "*2\r\n$2\r\nx3\r\n"
              "*0\r\n");
}

// Nonexistent key returns the key with an empty entries array.
void test_xread_missing_key(std::unordered_map<std::string, CommandHandler>& cmd) {
    ASSERT_EQ(cmd["XREAD"]({"XREAD", "STREAMS", "no_such", "0-0"}),
              "*1\r\n"
              "*2\r\n$7\r\nno_such\r\n"
              "*0\r\n");
}

// Multiple streams in one call.
void test_xread_multiple_streams(std::unordered_map<std::string, CommandHandler>& cmd) {
    cmd["XADD"]({"XADD", "xa", "1-0", "k", "v1"});
    cmd["XADD"]({"XADD", "xb", "2-0", "k", "v2"});
    ASSERT_EQ(cmd["XREAD"]({"XREAD", "STREAMS", "xa", "xb", "0-0", "0-0"}),
              "*2\r\n"
              "*2\r\n$2\r\nxa\r\n"
              "*1\r\n*2\r\n$3\r\n1-0\r\n*2\r\n$1\r\nk\r\n$2\r\nv1\r\n"
              "*2\r\n$2\r\nxb\r\n"
              "*1\r\n*2\r\n$3\r\n2-0\r\n*2\r\n$1\r\nk\r\n$2\r\nv2\r\n");
}

// Multiple fields per entry are all included.
void test_xread_multiple_fields(std::unordered_map<std::string, CommandHandler>& cmd) {
    cmd["XADD"]({"XADD", "x4", "1-0", "temperature", "37", "humidity", "94"});
    ASSERT_EQ(cmd["XREAD"]({"XREAD", "STREAMS", "x4", "0-0"}),
              "*1\r\n"
              "*2\r\n$2\r\nx4\r\n"
              "*1\r\n"
              "*2\r\n$3\r\n1-0\r\n"
              "*4\r\n$11\r\ntemperature\r\n$2\r\n37\r\n$8\r\nhumidity\r\n$2\r\n94\r\n");
}

// Wrong number of arguments.
void test_xread_wrong_args(std::unordered_map<std::string, CommandHandler>& cmd) {
    ASSERT_EQ(cmd["XREAD"]({"XREAD", "STREAMS", "k"}),
              "-ERR wrong number of arguments for XREAD\r\n");
}

// Missing STREAMS keyword.
void test_xread_missing_streams_kw(std::unordered_map<std::string, CommandHandler>& cmd) {
    ASSERT_EQ(cmd["XREAD"]({"XREAD", "NOTSTREAMS", "k", "0-0"}),
              "-ERR syntax error\r\n");
}

// Odd number of key/id args after STREAMS.
void test_xread_odd_args(std::unordered_map<std::string, CommandHandler>& cmd) {
    ASSERT_EQ(cmd["XREAD"]({"XREAD", "STREAMS", "k1", "k2", "0-0"}),
              "-ERR syntax error\r\n");
}

// Concurrency: one thread writes while another reads.
void test_xread_concurrent(std::unordered_map<std::string, CommandHandler>& cmd) {
    cmd["XADD"]({"XADD", "x_conc", "1-0", "k", "v"});

    std::string result;
    std::thread writer([&]{ cmd["XADD"]({"XADD", "x_conc", "2-0", "k", "v2"}); });
    std::thread reader([&]{ result = cmd["XREAD"]({"XREAD", "STREAMS", "x_conc", "0-0"}); });
    writer.join();
    reader.join();

    ASSERT_TRUE(!result.empty() && result[0] == '*');
}

int main() {
    Store store;
    auto cmd = build_command_table(store);

    test_xread_basic(cmd);
    test_xread_exclusive(cmd);
    test_xread_no_new_entries(cmd);
    test_xread_missing_key(cmd);
    test_xread_multiple_streams(cmd);
    test_xread_multiple_fields(cmd);
    test_xread_wrong_args(cmd);
    test_xread_missing_streams_kw(cmd);
    test_xread_odd_args(cmd);
    test_xread_concurrent(cmd);

    RUN_TESTS();
}
