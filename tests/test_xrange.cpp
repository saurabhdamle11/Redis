#include "test_runner.h"
#include "commands/commands.h"
#include "store/store.h"
#include <thread>

// Full range (- to +) returns all entries in insertion order.
void test_xrange_full(std::unordered_map<std::string, CommandHandler>& cmd) {
    cmd["XADD"]({"XADD", "r1", "1-0", "f", "a"});
    cmd["XADD"]({"XADD", "r1", "2-0", "f", "b"});
    cmd["XADD"]({"XADD", "r1", "3-0", "f", "c"});
    ASSERT_EQ(cmd["XRANGE"]({"XRANGE", "r1", "-", "+"}),
              "*3\r\n"
              "*2\r\n$3\r\n1-0\r\n*2\r\n$1\r\nf\r\n$1\r\na\r\n"
              "*2\r\n$3\r\n2-0\r\n*2\r\n$1\r\nf\r\n$1\r\nb\r\n"
              "*2\r\n$3\r\n3-0\r\n*2\r\n$1\r\nf\r\n$1\r\nc\r\n");
}

// Exact ID range returns only entries within [start, end].
void test_xrange_exact(std::unordered_map<std::string, CommandHandler>& cmd) {
    cmd["XADD"]({"XADD", "r2", "1-0", "f", "a"});
    cmd["XADD"]({"XADD", "r2", "2-0", "f", "b"});
    cmd["XADD"]({"XADD", "r2", "3-0", "f", "c"});
    ASSERT_EQ(cmd["XRANGE"]({"XRANGE", "r2", "1-0", "2-0"}),
              "*2\r\n"
              "*2\r\n$3\r\n1-0\r\n*2\r\n$1\r\nf\r\n$1\r\na\r\n"
              "*2\r\n$3\r\n2-0\r\n*2\r\n$1\r\nf\r\n$1\r\nb\r\n");
}

// Start equals end returns a single entry.
void test_xrange_single(std::unordered_map<std::string, CommandHandler>& cmd) {
    cmd["XADD"]({"XADD", "r3", "5-3", "k", "v"});
    ASSERT_EQ(cmd["XRANGE"]({"XRANGE", "r3", "5-3", "5-3"}),
              "*1\r\n"
              "*2\r\n$3\r\n5-3\r\n*2\r\n$1\r\nk\r\n$1\r\nv\r\n");
}

// Range with no matching entries returns empty array.
void test_xrange_no_match(std::unordered_map<std::string, CommandHandler>& cmd) {
    cmd["XADD"]({"XADD", "r4", "1-0", "f", "v"});
    ASSERT_EQ(cmd["XRANGE"]({"XRANGE", "r4", "5-0", "9-0"}), "*0\r\n");
}

// Nonexistent key returns empty array.
void test_xrange_missing_key(std::unordered_map<std::string, CommandHandler>& cmd) {
    ASSERT_EQ(cmd["XRANGE"]({"XRANGE", "no_such_key", "-", "+"}), "*0\r\n");
}

// Partial ID (no seq) as start defaults to seq=0.
void test_xrange_partial_start(std::unordered_map<std::string, CommandHandler>& cmd) {
    cmd["XADD"]({"XADD", "r5", "3-0", "f", "a"});
    cmd["XADD"]({"XADD", "r5", "3-1", "f", "b"});
    cmd["XADD"]({"XADD", "r5", "5-0", "f", "c"});
    // "3" as start → 3-0; "4" as end → 4-UINT64_MAX (no entry matches, so just 3-0 and 3-1)
    ASSERT_EQ(cmd["XRANGE"]({"XRANGE", "r5", "3", "4"}),
              "*2\r\n"
              "*2\r\n$3\r\n3-0\r\n*2\r\n$1\r\nf\r\n$1\r\na\r\n"
              "*2\r\n$3\r\n3-1\r\n*2\r\n$1\r\nf\r\n$1\r\nb\r\n");
}

// Multiple fields per entry are all returned.
void test_xrange_multiple_fields(std::unordered_map<std::string, CommandHandler>& cmd) {
    cmd["XADD"]({"XADD", "r6", "1-0", "a", "1", "b", "2"});
    ASSERT_EQ(cmd["XRANGE"]({"XRANGE", "r6", "-", "+"}),
              "*1\r\n"
              "*2\r\n$3\r\n1-0\r\n*4\r\n$1\r\na\r\n$1\r\n1\r\n$1\r\nb\r\n$1\r\n2\r\n");
}

// Wrong number of arguments.
void test_xrange_wrong_args(std::unordered_map<std::string, CommandHandler>& cmd) {
    ASSERT_EQ(cmd["XRANGE"]({"XRANGE", "k", "-"}),
              "-ERR wrong number of arguments for XRANGE\r\n");
}

// Concurrency: one thread writes while another reads — both must not crash
// and the reader must see a valid RESP array response.
void test_xrange_concurrent(std::unordered_map<std::string, CommandHandler>& cmd) {
    cmd["XADD"]({"XADD", "r_conc", "1-0", "k", "v"});

    std::string read_result;
    std::thread writer([&]{ cmd["XADD"]({"XADD", "r_conc", "2-0", "k", "v2"}); });
    std::thread reader([&]{ read_result = cmd["XRANGE"]({"XRANGE", "r_conc", "-", "+"}); });
    writer.join();
    reader.join();

    ASSERT_TRUE(!read_result.empty() && read_result[0] == '*');
}

int main() {
    Store store;
    auto cmd = build_command_table(store);

    test_xrange_full(cmd);
    test_xrange_exact(cmd);
    test_xrange_single(cmd);
    test_xrange_no_match(cmd);
    test_xrange_missing_key(cmd);
    test_xrange_partial_start(cmd);
    test_xrange_multiple_fields(cmd);
    test_xrange_wrong_args(cmd);
    test_xrange_concurrent(cmd);

    RUN_TESTS();
}
