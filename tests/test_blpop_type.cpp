#include "test_runner.h"
#include "commands/commands.h"
#include "store/store.h"
#include <chrono>
#include <string>
#include <thread>
#include <unistd.h>

// -----------------------------------------------------------------------
// TYPE tests
// -----------------------------------------------------------------------

void test_type_string(std::unordered_map<std::string, CommandHandler>& cmd) {
    cmd["SET"]({"SET", "t_str", "hello"});
    ASSERT_EQ(cmd["TYPE"]({"TYPE", "t_str"}), "+string\r\n");
}

void test_type_list(std::unordered_map<std::string, CommandHandler>& cmd) {
    cmd["RPUSH"]({"RPUSH", "t_list", "x"});
    ASSERT_EQ(cmd["TYPE"]({"TYPE", "t_list"}), "+list\r\n");
}

void test_type_none(std::unordered_map<std::string, CommandHandler>& cmd) {
    ASSERT_EQ(cmd["TYPE"]({"TYPE", "no_such_key"}), "+none\r\n");
}

void test_type_wrong_args(std::unordered_map<std::string, CommandHandler>& cmd) {
    ASSERT_EQ(cmd["TYPE"]({"TYPE"}),
              "-ERR wrong number of arguments for TYPE\r\n");
}

// -----------------------------------------------------------------------
// BLPOP tests
// -----------------------------------------------------------------------

// Data already in the list -- should return immediately without blocking.
void test_blpop_fast_path(std::unordered_map<std::string, CommandHandler>& cmd) {
    cmd["RPUSH"]({"RPUSH", "fp_list", "immediate"});
    ASSERT_EQ(cmd["BLPOP"]({"BLPOP", "fp_list", "1"}),
              "*2\r\n$7\r\nfp_list\r\n$9\r\nimmediate\r\n");
}

// Wrong number of arguments.
void test_blpop_wrong_args(std::unordered_map<std::string, CommandHandler>& cmd) {
    ASSERT_EQ(cmd["BLPOP"]({"BLPOP", "k"}),
              "-ERR wrong number of arguments for BLPOP\r\n");
}

// BLPOP blocks on an empty list, then RPUSH unblocks it.
void test_blpop_blocks_then_rpush(std::unordered_map<std::string, CommandHandler>& cmd) {
    std::string result;
    std::thread t([&]{ result = cmd["BLPOP"]({"BLPOP", "bl_list", "5"}); });

    usleep(50'000); // 50 ms -- let thread enter the wait queue
    cmd["RPUSH"]({"RPUSH", "bl_list", "hello"});
    t.join();

    ASSERT_EQ(result, "*2\r\n$7\r\nbl_list\r\n$5\r\nhello\r\n");
}

// BLPOP blocks on an empty list, then LPUSH unblocks it.
void test_blpop_blocks_then_lpush(std::unordered_map<std::string, CommandHandler>& cmd) {
    std::string result;
    std::thread t([&]{ result = cmd["BLPOP"]({"BLPOP", "bl_list2", "5"}); });

    usleep(50'000);
    cmd["LPUSH"]({"LPUSH", "bl_list2", "world"});
    t.join();

    ASSERT_EQ(result, "*2\r\n$8\r\nbl_list2\r\n$5\r\nworld\r\n");
}

// No data pushed within the timeout window -- must return null array.
void test_blpop_timeout(std::unordered_map<std::string, CommandHandler>& cmd) {
    auto start  = std::chrono::steady_clock::now();
    std::string result = cmd["BLPOP"]({"BLPOP", "timeout_list", "0.1"}); // 100 ms
    auto elapsed = std::chrono::steady_clock::now() - start;

    ASSERT_EQ(result, "*-1\r\n");
    ASSERT_TRUE(elapsed >= std::chrono::milliseconds(100));
}

// Two threads block on the same key. The one that registered first must get
// the first pushed element; the second gets the second.
void test_blpop_fifo_fairness(std::unordered_map<std::string, CommandHandler>& cmd) {
    std::string r1, r2;

    std::thread t1([&]{ r1 = cmd["BLPOP"]({"BLPOP", "fair_list", "5"}); });
    usleep(50'000); // ensure t1 registers before t2
    std::thread t2([&]{ r2 = cmd["BLPOP"]({"BLPOP", "fair_list", "5"}); });
    usleep(50'000); // ensure t2 is in the queue before we push

    cmd["RPUSH"]({"RPUSH", "fair_list", "first"});
    usleep(20'000); // let t1 wake and finish
    cmd["RPUSH"]({"RPUSH", "fair_list", "second"});

    t1.join();
    t2.join();

    ASSERT_EQ(r1, "*2\r\n$9\r\nfair_list\r\n$5\r\nfirst\r\n");
    ASSERT_EQ(r2, "*2\r\n$9\r\nfair_list\r\n$6\r\nsecond\r\n");
}

// -----------------------------------------------------------------------
// Main
// -----------------------------------------------------------------------

int main() {
    Store store;
    auto cmd = build_command_table(store);

    // TYPE
    test_type_string(cmd);
    test_type_list(cmd);
    test_type_none(cmd);
    test_type_wrong_args(cmd);

    // BLPOP
    test_blpop_wrong_args(cmd);
    test_blpop_fast_path(cmd);
    test_blpop_blocks_then_rpush(cmd);
    test_blpop_blocks_then_lpush(cmd);
    test_blpop_timeout(cmd);
    test_blpop_fifo_fairness(cmd);

    RUN_TESTS();
}
