# Redis built from Scratch in C++

A ground-up implementation of a Redis compatible in-memory server written in C++.

## What it does

- Listens on port **6379** (the default Redis port)
- Speaks the **RESP (Redis Serialization Protocol)** wire format
- Handles multiple clients concurrently using **POSIX threads** (one detached thread per connection)
- Protects shared state with a mutex for thread-safe access
- Supports the following commands:
  - `PING` responds with `+PONG`
  - `ECHO <message>` echoes the message back as a bulk string
  - `SET <key> <value> [EX seconds | PX milliseconds]` stores a key with an optional TTL
  - `GET <key>` retrieves a value, returning nil if the key is missing or expired
  - `RPUSH <key> <value> [value ...]` appends one or more elements to the tail of a list
  - `LPUSH <key> <value> [value ...]` prepends one or more elements to the head of a list
  - `LLEN <key>` returns the length of a list
  - `LRANGE <key> start stop` returns a range of elements from a list (negative indices supported)

## How to build and run

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

Then connect with the official Redis CLI or `redis-cli`:

```bash
redis-cli PING
redis-cli ECHO "hello"
redis-cli SET foo bar EX 30
redis-cli GET foo
redis-cli RPUSH mylist a b c
redis-cli LPUSH mylist z
redis-cli LRANGE mylist 0 -1
redis-cli LLEN mylist
```

## Project structure

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
│   │   └── commands.cpp            # Command handlers and command table
│   └── server/
│       ├── server.h
│       └── server.cpp              # Per-client handler
└── tests/
    ├── test_runner.h               # Lightweight ASSERT_EQ / RUN_TESTS macros
    ├── test_resp.cpp               # RESP parser tests
    └── test_commands.cpp           # Command handler tests
```

## Architecture

| Layer | Location | Responsibility |
|-------|----------|----------------|
| Entry point | `main.cpp` | Socket setup, bind, listen, accept loop |
| RESP parser | `src/resp/` | Decodes raw bytes into a vector of string tokens |
| Store | `src/store/` | Thread-safe KV and list storage with TTL eviction |
| Commands | `src/commands/` | Maps command names to handler functions (O(1) dispatch) |
| Server | `src/server/` | Reads from a client socket, dispatches commands, sends responses |

