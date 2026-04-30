#include "test_runner.h"
#include "resp/resp.h"

void test_ping() {
    auto t = parse_resp("*1\r\n$4\r\nPING\r\n");
    ASSERT_EQ(t.size(), 1u);
    ASSERT_EQ(t[0], "PING");
}

void test_echo_two_tokens() {
    auto t = parse_resp("*2\r\n$4\r\nECHO\r\n$5\r\nhello\r\n");
    ASSERT_EQ(t.size(), 2u);
    ASSERT_EQ(t[0], "ECHO");
    ASSERT_EQ(t[1], "hello");
}

void test_set_with_expiry() {
    auto t = parse_resp("*5\r\n$3\r\nSET\r\n$3\r\nfoo\r\n$3\r\nbar\r\n$2\r\nEX\r\n$2\r\n30\r\n");
    ASSERT_EQ(t.size(), 5u);
    ASSERT_EQ(t[0], "SET");
    ASSERT_EQ(t[1], "foo");
    ASSERT_EQ(t[2], "bar");
    ASSERT_EQ(t[3], "EX");
    ASSERT_EQ(t[4], "30");
}

void test_rpush_multi_value() {
    auto t = parse_resp("*5\r\n$5\r\nRPUSH\r\n$4\r\nlist\r\n$1\r\na\r\n$1\r\nb\r\n$1\r\nc\r\n");
    ASSERT_EQ(t.size(), 5u);
    ASSERT_EQ(t[0], "RPUSH");
    ASSERT_EQ(t[4], "c");
}

void test_empty_input() {
    auto t = parse_resp("");
    ASSERT_EQ(t.size(), 0u);
}

void test_non_array_input() {
    auto t = parse_resp("+OK\r\n");
    ASSERT_EQ(t.size(), 0u);
}

int main() {
    test_ping();
    test_echo_two_tokens();
    test_set_with_expiry();
    test_rpush_multi_value();
    test_empty_input();
    test_non_array_input();
    RUN_TESTS();
}
