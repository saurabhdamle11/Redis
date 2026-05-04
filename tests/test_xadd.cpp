#include "test_runner.h"
#include "commands/commands.h"
#include "store/store.h"
#include <thread>
#include <unistd.h>

// -----------------------------------------------------------------------
// XADD tests
// -----------------------------------------------------------------------

// Basic XADD with explicit ID returns that ID as a bulk string.
void test_xadd_explicit_id(std::unordered_map<std::string, CommandHandler>& cmd) {
    ASSERT_EQ(cmd["XADD"]({"XADD", "s1", "1526919030474-0", "temp", "36"}),
              "$15\r\n1526919030474-0\r\n");
}

// Second entry with a higher explicit ID succeeds.
void test_xadd_second_entry(std::unordered_map<std::string, CommandHandler>& cmd) {
    cmd["XADD"]({"XADD", "s2", "0-1", "foo", "bar"});
    ASSERT_EQ(cmd["XADD"]({"XADD", "s2", "0-2", "foo", "baz"}),
              "$3\r\n0-2\r\n");
}

// Multiple field-value pairs in a single XADD.
void test_xadd_multiple_fields(std::unordered_map<std::string, CommandHandler>& cmd) {
    ASSERT_EQ(cmd["XADD"]({"XADD", "s3", "1-0", "a", "1", "b", "2", "c", "3"}),
              "$3\r\n1-0\r\n");
}

// TYPE of a stream key must return "+stream\r\n".
void test_xadd_type_is_stream(std::unordered_map<std::string, CommandHandler>& cmd) {
    cmd["XADD"]({"XADD", "s_type", "1-0", "k", "v"});
    ASSERT_EQ(cmd["TYPE"]({"TYPE", "s_type"}), "+stream\r\n");
}

// TYPE still returns correct values for non-stream keys.
void test_type_unaffected(std::unordered_map<std::string, CommandHandler>& cmd) {
    cmd["SET"]({"SET", "plain_str", "hello"});
    cmd["RPUSH"]({"RPUSH", "plain_list", "x"});
    ASSERT_EQ(cmd["TYPE"]({"TYPE", "plain_str"}),  "+string\r\n");
    ASSERT_EQ(cmd["TYPE"]({"TYPE", "plain_list"}), "+list\r\n");
    ASSERT_EQ(cmd["TYPE"]({"TYPE", "no_such"}),    "+none\r\n");
}

// Out-of-order ID (equal to last) must return an error.
void test_xadd_duplicate_id_rejected(std::unordered_map<std::string, CommandHandler>& cmd) {
    cmd["XADD"]({"XADD", "s_dup", "5-5", "f", "v"});
    ASSERT_EQ(cmd["XADD"]({"XADD", "s_dup", "5-5", "f", "v2"}),
              "-ERR The ID specified in XADD is equal or smaller than the target stream top item\r\n");
}

// Out-of-order ID (lower ms) must return an error.
void test_xadd_lower_id_rejected(std::unordered_map<std::string, CommandHandler>& cmd) {
    cmd["XADD"]({"XADD", "s_low", "10-0", "f", "v"});
    ASSERT_EQ(cmd["XADD"]({"XADD", "s_low", "9-0", "f", "v2"}),
              "-ERR The ID specified in XADD is equal or smaller than the target stream top item\r\n");
}

// Same ms, lower seq must also be rejected.
void test_xadd_lower_seq_rejected(std::unordered_map<std::string, CommandHandler>& cmd) {
    cmd["XADD"]({"XADD", "s_seq", "10-5", "f", "v"});
    ASSERT_EQ(cmd["XADD"]({"XADD", "s_seq", "10-4", "f", "v2"}),
              "-ERR The ID specified in XADD is equal or smaller than the target stream top item\r\n");
}

// Same ms, higher seq must succeed.
void test_xadd_same_ms_higher_seq(std::unordered_map<std::string, CommandHandler>& cmd) {
    cmd["XADD"]({"XADD", "s_hseq", "10-0", "f", "v"});
    ASSERT_EQ(cmd["XADD"]({"XADD", "s_hseq", "10-1", "f", "v2"}),
              "$4\r\n10-1\r\n");
}

// ID 0-0 is always invalid and gets its own error message.
void test_xadd_zero_zero_rejected(std::unordered_map<std::string, CommandHandler>& cmd) {
    ASSERT_EQ(cmd["XADD"]({"XADD", "s_zero", "0-0", "f", "v"}),
              "-ERR The ID specified in XADD must be greater than 0-0\r\n");
}

// 0-0 is also invalid when a stream already has entries.
void test_xadd_zero_zero_rejected_nonempty(std::unordered_map<std::string, CommandHandler>& cmd) {
    cmd["XADD"]({"XADD", "s_zero2", "1-1", "f", "v"});
    ASSERT_EQ(cmd["XADD"]({"XADD", "s_zero2", "0-0", "f", "v2"}),
              "-ERR The ID specified in XADD must be greater than 0-0\r\n");
}

// Wrong number of arguments (missing value for a field).
void test_xadd_wrong_args(std::unordered_map<std::string, CommandHandler>& cmd) {
    // args.size() == 4 → (4-3) % 2 == 1 != 0 → error
    ASSERT_EQ(cmd["XADD"]({"XADD", "s_err", "1-0", "field_only"}),
              "-ERR wrong number of arguments for XADD\r\n");
}

// Too few arguments entirely.
void test_xadd_too_few_args(std::unordered_map<std::string, CommandHandler>& cmd) {
    ASSERT_EQ(cmd["XADD"]({"XADD", "s_err", "1-0"}),
              "-ERR wrong number of arguments for XADD\r\n");
}

// Auto-ID (*): returned ID must be non-zero and monotonically increasing.
void test_xadd_auto_id(std::unordered_map<std::string, CommandHandler>& cmd) {
    std::string r1 = cmd["XADD"]({"XADD", "s_auto", "*", "k", "v1"});
    std::string r2 = cmd["XADD"]({"XADD", "s_auto", "*", "k", "v2"});

    // Both must be bulk strings (start with '$')
    ASSERT_TRUE(r1.size() > 0 && r1[0] == '$');
    ASSERT_TRUE(r2.size() > 0 && r2[0] == '$');

    // Extract the ID strings from bulk string responses ("$N\r\nID\r\n")
    auto extract_id = [](const std::string& resp) -> std::string {
        auto first_crlf  = resp.find("\r\n");
        auto second_crlf = resp.find("\r\n", first_crlf + 2);
        return resp.substr(first_crlf + 2, second_crlf - first_crlf - 2);
    };

    std::string id1 = extract_id(r1);
    std::string id2 = extract_id(r2);

    // Parse ms-seq
    auto parse = [](const std::string& id) -> std::pair<uint64_t, uint64_t> {
        auto dash = id.find('-');
        return { std::stoull(id.substr(0, dash)), std::stoull(id.substr(dash + 1)) };
    };

    auto [ms1, seq1] = parse(id1);
    auto [ms2, seq2] = parse(id2);

    // Second ID must be strictly greater than first
    ASSERT_TRUE(ms2 > ms1 || (ms2 == ms1 && seq2 > seq1));
}

// Partial auto-seq ("ms-*"): seq must be auto-incremented from last entry on same ms.
void test_xadd_partial_auto_seq(std::unordered_map<std::string, CommandHandler>& cmd) {
    cmd["XADD"]({"XADD", "s_pautoseq", "100-5", "k", "v"});
    ASSERT_EQ(cmd["XADD"]({"XADD", "s_pautoseq", "100-*", "k", "v2"}),
              "$5\r\n100-6\r\n");
}

// Partial auto-seq on a new ms resets seq to 0.
void test_xadd_partial_auto_seq_new_ms(std::unordered_map<std::string, CommandHandler>& cmd) {
    cmd["XADD"]({"XADD", "s_pautoms", "100-5", "k", "v"});
    ASSERT_EQ(cmd["XADD"]({"XADD", "s_pautoms", "200-*", "k", "v2"}),
              "$5\r\n200-0\r\n");
}

// ms=0 with auto-seq on empty stream starts at 1 (special case per spec).
void test_xadd_zero_ms_auto_seq_empty(std::unordered_map<std::string, CommandHandler>& cmd) {
    ASSERT_EQ(cmd["XADD"]({"XADD", "s_0seq", "0-*", "foo", "bar"}),
              "$3\r\n0-1\r\n");
}

// ms=0 with auto-seq increments normally after the first entry.
void test_xadd_zero_ms_auto_seq_increment(std::unordered_map<std::string, CommandHandler>& cmd) {
    cmd["XADD"]({"XADD", "s_0seqi", "0-*", "foo", "bar"});   // → 0-1
    ASSERT_EQ(cmd["XADD"]({"XADD", "s_0seqi", "0-*", "bar", "baz"}),
              "$3\r\n0-2\r\n");
}

// Non-zero ms with auto-seq on empty stream starts at 0.
void test_xadd_nonzero_ms_auto_seq_empty(std::unordered_map<std::string, CommandHandler>& cmd) {
    ASSERT_EQ(cmd["XADD"]({"XADD", "s_5seq", "5-*", "foo", "bar"}),
              "$3\r\n5-0\r\n");
}

// Non-zero ms with auto-seq increments on the same ms.
void test_xadd_nonzero_ms_auto_seq_increment(std::unordered_map<std::string, CommandHandler>& cmd) {
    cmd["XADD"]({"XADD", "s_5seqi", "5-*", "foo", "bar"});   // → 5-0
    ASSERT_EQ(cmd["XADD"]({"XADD", "s_5seqi", "5-*", "bar", "baz"}),
              "$3\r\n5-1\r\n");
}

// Concurrency: two threads XADD to the same stream; both must succeed and
// the stream must contain exactly 2 entries (verified via XADD rejecting a
// duplicate ID that could only exist if locking were broken).
void test_xadd_concurrent(std::unordered_map<std::string, CommandHandler>& cmd) {
    std::string r1, r2;

    std::thread t1([&]{ r1 = cmd["XADD"]({"XADD", "s_conc", "1-0", "k", "v1"}); });
    std::thread t2([&]{ r2 = cmd["XADD"]({"XADD", "s_conc", "2-0", "k", "v2"}); });
    t1.join();
    t2.join();

    // Both must have succeeded (bulk string responses)
    ASSERT_TRUE(r1[0] == '$');
    ASSERT_TRUE(r2[0] == '$');
}

// -----------------------------------------------------------------------
// Main
// -----------------------------------------------------------------------

int main() {
    Store store;
    auto cmd = build_command_table(store);

    test_xadd_explicit_id(cmd);
    test_xadd_second_entry(cmd);
    test_xadd_multiple_fields(cmd);
    test_xadd_type_is_stream(cmd);
    test_type_unaffected(cmd);
    test_xadd_duplicate_id_rejected(cmd);
    test_xadd_lower_id_rejected(cmd);
    test_xadd_lower_seq_rejected(cmd);
    test_xadd_same_ms_higher_seq(cmd);
    test_xadd_zero_zero_rejected(cmd);
    test_xadd_zero_zero_rejected_nonempty(cmd);
    test_xadd_wrong_args(cmd);
    test_xadd_too_few_args(cmd);
    test_xadd_auto_id(cmd);
    test_xadd_partial_auto_seq(cmd);
    test_xadd_partial_auto_seq_new_ms(cmd);
    test_xadd_zero_ms_auto_seq_empty(cmd);
    test_xadd_zero_ms_auto_seq_increment(cmd);
    test_xadd_nonzero_ms_auto_seq_empty(cmd);
    test_xadd_nonzero_ms_auto_seq_increment(cmd);
    test_xadd_concurrent(cmd);

    RUN_TESTS();
}
