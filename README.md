# Redis built from Scratch in C++

A ground-up implementation of a Redis compatible in-memory server written in C++.

## What it does

- Listens on port **6379** (the default Redis port)
- Speaks the **RESP (Redis Serialization Protocol)** wire format
- Handles multiple clients concurrently using **POSIX threads** (one detached thread per connection)
- Supports the following commands:
  - `PING` — responds with `+PONG`
  - `ECHO <message>` — echoes the message back as a bulk string

## How to build and run

```bash
g++ -std=c++17 -pthread -o redis-server main.cpp
./redis-server
```

Then connect with the official Redis CLI or `redis-cli`:

```bash
redis-cli PING
redis-cli ECHO "hello"
```

## Architecture

| Component | File | Description |
|-----------|------|-------------|
| TCP server | `main.cpp` | Creates a socket, binds to port 6379, and accepts connections in a loop |
| RESP parser | `parse_resp()` | Parses incoming RESP arrays into a vector of string tokens |
| Client handler | `handle_client()` | Reads commands from a connected client and dispatches responses |

## Roadmap

- [ ] `SET` / `GET` commands with an in-memory hash map
- [ ] Key expiry (`SET key value EX seconds`)
- [ ] `DEL`, `EXISTS`, `KEYS` commands
- [ ] Persistence (RDB / AOF snapshots)
- [ ] Pub/Sub
