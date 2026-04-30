#include "test_runner.h"
#include "commands/commands.h"
#include "store/store.h"
#include <unistd.h>

int main() {
    Store store;
    auto cmd = build_command_table(store);

    // PING
    ASSERT_EQ(cmd["PING"]({"PING"}), "+PONG\r\n");

    // ECHO
    ASSERT_EQ(cmd["ECHO"]({"ECHO", "hello"}),   "$5\r\nhello\r\n");
    ASSERT_EQ(cmd["ECHO"]({"ECHO", ""}),         "$0\r\n\r\n");
    ASSERT_EQ(cmd["ECHO"]({"ECHO"}),
              "-ERR wrong number of arguments for ECHO\r\n");

    // SET / GET round-trip
    ASSERT_EQ(cmd["SET"]({"SET", "k", "v"}),    "+OK\r\n");
    ASSERT_EQ(cmd["GET"]({"GET", "k"}),          "$1\r\nv\r\n");

    // GET missing key
    ASSERT_EQ(cmd["GET"]({"GET", "no_such_key"}), "$-1\r\n");

    // SET overwrites
    ASSERT_EQ(cmd["SET"]({"SET", "k", "v2"}),   "+OK\r\n");
    ASSERT_EQ(cmd["GET"]({"GET", "k"}),          "$2\r\nv2\r\n");

    // SET with PX expiry (1 ms) then GET should return nil
    ASSERT_EQ(cmd["SET"]({"SET", "exp", "gone", "PX", "1"}), "+OK\r\n");
    usleep(5000); // 5 ms
    ASSERT_EQ(cmd["GET"]({"GET", "exp"}), "$-1\r\n");

    // RPUSH
    ASSERT_EQ(cmd["RPUSH"]({"RPUSH", "mylist", "a", "b", "c"}), ":3\r\n");
    ASSERT_EQ(cmd["RPUSH"]({"RPUSH", "mylist", "d"}),            ":4\r\n");
    ASSERT_EQ(cmd["RPUSH"]({"RPUSH"}),
              "-ERR wrong number of arguments for RPUSH\r\n");

    // LPUSH prepends
    ASSERT_EQ(cmd["LPUSH"]({"LPUSH", "mylist", "z"}), ":5\r\n");

    // LLEN
    ASSERT_EQ(cmd["LLEN"]({"LLEN", "mylist"}),    ":5\r\n");
    ASSERT_EQ(cmd["LLEN"]({"LLEN", "no_list"}),   ":0\r\n");

    // LRANGE full list
    ASSERT_EQ(cmd["LRANGE"]({"LRANGE", "mylist", "0", "-1"}),
              "*5\r\n$1\r\nz\r\n$1\r\na\r\n$1\r\nb\r\n$1\r\nc\r\n$1\r\nd\r\n");

    // LRANGE sub-range
    ASSERT_EQ(cmd["LRANGE"]({"LRANGE", "mylist", "1", "2"}),
              "*2\r\n$1\r\na\r\n$1\r\nb\r\n");

    // LRANGE out-of-bounds
    ASSERT_EQ(cmd["LRANGE"]({"LRANGE", "no_list", "0", "-1"}), "*0\r\n");

    RUN_TESTS();
}
