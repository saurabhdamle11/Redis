# Redis built from Scratch in C++

A ground-up implementation of a Redis-compatible in-memory server written in modern C++17.

---

## Branches

| Branch | Concurrency model | Recommended for |
|--------|-------------------|-----------------|
| `main` | One detached thread per connection | Learning, experimentation |
| `epoll` | Single-threaded async I/O via **kqueue** + non-blocking sockets | **Production deployments** |

The `main` branch is intentionally simple and a good starting point for understanding the architecture. The `epoll` branch replaces the thread-per-client model with a kqueue event loop, adds partial-read-safe RESP parsing, per-connection read/write buffering, and full ACL-based authentication and authorization — all with no threads and no blocking I/O.

**Use the `epoll` branch for any real deployment.**

---

## Implemented Commands

| Command | Syntax | Notes |
|---------|--------|-------|
| `PING` | `PING` | Returns `+PONG` |
| `ECHO` | `ECHO <msg>` | Returns bulk string |
| `SET` | `SET <key> <val> [EX s \| PX ms]` | Optional TTL |
| `GET` | `GET <key>` | Returns nil if expired or missing |
| `RPUSH` | `RPUSH <key> <val> [val ...]` | Appends to list tail |
| `LPUSH` | `LPUSH <key> <val> [val ...]` | Prepends to list head |
| `LLEN` | `LLEN <key>` | Returns 0 for missing keys |
| `LRANGE` | `LRANGE <key> start stop` | Supports negative indices |
| `TYPE` | `TYPE <key>` | Returns `string`, `list`, `stream`, or `none` |
| `BLPOP` | `BLPOP <key> <timeout_sec>` | Blocking pop with FIFO fairness |
| `XADD` | `XADD <key> <id\|*\|ms-*> field val [...]` | Append stream entry; auto-ID supported |
| `XRANGE` | `XRANGE <key> <start\|-\> <end\|+\>` | Inclusive range query on a stream |
| `XREAD` | `XREAD STREAMS <key> [key ...] <id> [id ...]` | Exclusive read from one or more streams |
| `AUTH` | `AUTH [username] <password>` | Authenticate connection; defaults to user `default` (`epoll` branch only) |
| `ACL WHOAMI` | `ACL WHOAMI` | Return the current connection's username |
| `ACL LIST` | `ACL LIST` | Return all users in ACL-file rule format |
| `ACL USERS` | `ACL USERS` | Return all configured usernames |
| `ACL GETUSER` | `ACL GETUSER <name>` | Return flags, passwords, commands, and keys for a user |
| `ACL SETUSER` | `ACL SETUSER <name> [rules ...]` | Create or modify a user (e.g. `on >pw ~prefix:* +@read`) |
| `ACL DELUSER` | `ACL DELUSER <name> [name ...]` | Delete one or more users (cannot delete `default`) |
| `ACL CAT` | `ACL CAT [category]` | List categories, or commands in a category |
| `ACL SAVE` | `ACL SAVE` | Persist current ACL state to `REDIS_ACLFILE` |
| `ACL LOAD` | `ACL LOAD` | Reload ACL state from `REDIS_ACLFILE` |

---

## How to Build and Run

```bash
make
./redis-server
```

To run the tests:

```bash
make test
```

To clean build artifacts:

```bash
make clean
```

`REDIS_PASSWORD` is the legacy single-password mode: it tightens the built-in `default` user to require the given password and grant full access (`~* +@all`). Clients must then send `AUTH secret` (or `AUTH default secret`) before any other command. If neither `REDIS_PASSWORD` nor `REDIS_ACLFILE` is set, the `default` user remains open (`on nopass ~* +@all`) and connections are accepted without authentication.

### Running with an ACL file (`epoll` branch)

```bash
REDIS_ACLFILE=/path/to/users.acl ./redis-server
```

If the file exists, users are loaded at startup; if it doesn't, the server keeps the built-in `default` user and logs a warning. `ACL SAVE` writes the current in-memory users back to this file, and `ACL LOAD` re-reads it. `REDIS_PASSWORD` and `REDIS_ACLFILE` can be combined — the password is applied to `default` first, then the ACL file is loaded on top.

Each line in the ACL file follows the same syntax as `ACL SETUSER`:

```
user default on nopass ~* +@all
user alice on >s3cr3t ~cache:* +@read +@write -flushdb
user readonly on >viewonly ~* +@read -@dangerous
```

Supported rules: `on` / `off`, `nopass`, `resetpass`, `>password` (plaintext, stored as SHA-256), `#hash` (pre-hashed), `~pattern` (key glob with `*` and `?`), `allkeys`, `+command` / `-command`, `+@category` / `-@category`, `allcommands` / `nocommands`. Categories include `read`, `write`, `keyspace`, `string`, `list`, `stream`, `connection`, `admin`, `dangerous`, and `all`.

---

## Connecting with redis-cli

```bash
redis-cli PING
redis-cli ECHO "hello"
redis-cli SET foo bar EX 30
redis-cli GET foo
redis-cli RPUSH mylist a b c
redis-cli LPUSH mylist z
redis-cli LRANGE mylist 0 -1
redis-cli LLEN mylist
redis-cli TYPE mylist
redis-cli BLPOP mylist 5
redis-cli XADD mystream "*" temperature 36 humidity 95
redis-cli XADD mystream "1-*" temperature 37
redis-cli XRANGE mystream - +
redis-cli XREAD STREAMS mystream 0-0

# epoll branch with auth (legacy single password)
redis-cli -a secret PING

# epoll branch with multi-user ACLs
redis-cli -u redis://alice:s3cr3t@localhost:6379 SET cache:foo bar
redis-cli -u redis://alice:s3cr3t@localhost:6379 ACL WHOAMI
redis-cli -a secret ACL SETUSER bob on '>hunter2' '~bob:*' +@read +@write
redis-cli -a secret ACL GETUSER bob
redis-cli -a secret ACL LIST
redis-cli -a secret ACL SAVE
```

---

## Project Structure

```
Redis/
├── main.cpp                        # Entry point: server setup and accept loop
├── Makefile
├── src/
│   ├── types.h                     # Shared type aliases (Args, CommandHandler)
│   ├── resp/
│   │   ├── resp.h
│   │   └── resp.cpp                # RESP protocol parser
│   ├── store/
│   │   ├── store.h
│   │   └── store.cpp               # In-memory Store class (KV + lists, mutex-guarded)
│   ├── commands/
│   │   ├── commands.h
│   │   └── commands.cpp        # Command handler lambdas registered at startup
│   ├── acl/                    # epoll branch only
│   │   ├── acl.h
│   │   └── acl.cpp             # Users, password hashing, command/key permissions, ACL file I/O
│   └── server/
│       ├── server.h
│       └── server.cpp          # Per-client loop (main) / kqueue Server class with ACL gating (epoll)
└── tests/
    ├── test_runner.h           # Lightweight ASSERT_EQ / RUN_TESTS macros
    ├── test_resp.cpp
    ├── test_commands.cpp
    ├── test_blpop_type.cpp
    ├── test_xadd.cpp
    ├── test_xrange.cpp
    ├── test_xread.cpp
    ├── test_auth.cpp           # epoll branch only
    └── test_acl.cpp            # epoll branch only
```

## Architecture

| Layer | Location | Responsibility |
|-------|----------|----------------|
| Entry point | `main.cpp` | Socket setup, bind, listen; spawns threads (`main`) or starts event loop (`epoll`) |
| RESP parser | `src/resp/` | Decodes raw bytes into string tokens; `try_parse_resp` handles partial network reads |
| Store | `src/store/` | Mutex-guarded KV, list, and stream storage with lazy TTL eviction |
| Commands | `src/commands/` | Maps command names to handler lambdas (O(1) dispatch) |
| ACL | `src/acl/` | Users, SHA-256 password hashing, command-category and key-pattern permissions, ACL file I/O (`epoll` only) |
| Server | `src/server/` | Blocking per-client read loop (`main`) / kqueue event loop with `Connection` buffers and per-connection ACL gating (`epoll`) |

---

## Concurrency and Thread Safety

All `Store` methods acquire a single global mutex for their full duration. This ensures correctness at the cost of throughput under high concurrency — every command serializes, even reads on different keys. This is an intentional simplicity trade-off; per-key locking would improve parallelism but adds significant complexity.

The `epoll` branch removes per-connection threads entirely. A single thread drives the kqueue event loop; the store mutex is still present to guard against any future threading additions.

---

## Known Limitations

- **No persistence** — all data is lost on restart (no RDB/AOF).
- **Lazy TTL expiry only** — expired keys are evicted on access, not in the background; memory is not reclaimed until a key is read.
- **Single global mutex** — all commands serialize on the store lock.
- **No TLS** — connections are plaintext, so ACL passwords travel in cleartext on the wire (they are SHA-256 hashed at rest).
- **RESP2 only** — RESP3 (used by newer clients) is not supported.
