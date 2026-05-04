# Redis built from Scratch in C++

A ground-up implementation of a Redis-compatible in-memory server written in modern C++17.

---

## Branches

| Branch | Concurrency model | Recommended for |
|--------|-------------------|-----------------|
| `main` | One detached thread per connection | Learning, experimentation |
| `epoll` | Single-threaded async I/O via **kqueue** + non-blocking sockets | **Production deployments** |

The `main` branch is intentionally simple and a good starting point for understanding the architecture. The `epoll` branch replaces the thread-per-client model with a kqueue event loop, adds partial-read-safe RESP parsing, per-connection read/write buffering, and password authentication — all with no threads and no blocking I/O.

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
| `AUTH` | `AUTH <password>` | Authenticate connection (`epoll` branch only) |

---

## How to Build and Run

```bash
make               # build redis-server binary
make test          # build and run all test suites
make clean         # remove build artifacts
```

### Running without authentication

```bash
./redis-server
```

### Running with password authentication (`epoll` branch)

```bash
REDIS_PASSWORD=secret ./redis-server
```

Clients must send `AUTH secret` before any other command. If `REDIS_PASSWORD` is not set, the server accepts all connections without authentication.

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

# epoll branch with auth
redis-cli -a secret PING
```

---

## Project Structure

```
Redis/
├── main.cpp                    # Entry point: socket setup + event loop (epoll) or accept loop (main)
├── Makefile
├── src/
│   ├── types.h                 # Shared type aliases (Args, CommandHandler)
│   ├── resp/
│   │   ├── resp.h
│   │   └── resp.cpp            # RESP protocol parser + partial-read-safe try_parse_resp
│   ├── store/
│   │   ├── store.h
│   │   └── store.cpp           # Thread-safe in-memory store (KV, lists, streams)
│   ├── commands/
│   │   ├── commands.h
│   │   └── commands.cpp        # Command handler lambdas registered at startup
│   └── server/
│       ├── server.h
│       └── server.cpp          # Per-client loop (main) / kqueue Server class (epoll)
└── tests/
    ├── test_runner.h           # Lightweight ASSERT_EQ / RUN_TESTS macros
    ├── test_resp.cpp
    ├── test_commands.cpp
    ├── test_blpop_type.cpp
    ├── test_xadd.cpp
    ├── test_xrange.cpp
    ├── test_xread.cpp
    └── test_auth.cpp           # epoll branch only
```

---

## Architecture

| Layer | Location | Responsibility |
|-------|----------|----------------|
| Entry point | `main.cpp` | Socket setup, bind, listen; spawns threads (`main`) or starts event loop (`epoll`) |
| RESP parser | `src/resp/` | Decodes raw bytes into string tokens; `try_parse_resp` handles partial network reads |
| Store | `src/store/` | Mutex-guarded KV, list, and stream storage with lazy TTL eviction |
| Commands | `src/commands/` | Maps command names to handler lambdas (O(1) dispatch) |
| Server | `src/server/` | Blocking per-client read loop (`main`) / kqueue event loop with `Connection` buffers (`epoll`) |

---

## Concurrency and Thread Safety

All `Store` methods acquire a single global mutex for their full duration. This ensures correctness at the cost of throughput under high concurrency — every command serializes, even reads on different keys. This is an intentional simplicity trade-off; per-key locking would improve parallelism but adds significant complexity.

The `epoll` branch removes per-connection threads entirely. A single thread drives the kqueue event loop; the store mutex is still present to guard against any future threading additions.

---

## Known Limitations

- **No persistence** — all data is lost on restart (no RDB/AOF).
- **Lazy TTL expiry only** — expired keys are evicted on access, not in the background; memory is not reclaimed until a key is read.
- **Single global mutex** — all commands serialize on the store lock.
- **No TLS** — connections are plaintext.
- **Password auth only** — no ACLs or per-user permissions.
- **RESP2 only** — RESP3 (used by newer clients) is not supported.
