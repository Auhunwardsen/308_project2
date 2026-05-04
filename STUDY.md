# Mini-KV viva study guide

A plain-language walkthrough of how my server works, stage by stage. Written so I can answer the viva questions without memorizing.

---

## 1. The big picture

My server is a TCP program that stores key-value pairs in memory. Clients connect, send text commands like `PUT color red`, and the server replies with `OK` or `VALUE red`. Lots of clients can connect at the same time. There is also a background thread that deletes keys whose TTL ran out.

Think of it like a tiny in-memory database with one big shared dictionary.

---

## 2. The threads (who does what)

There are three kinds of threads in my server:

1. **Main thread** -- only job is to call `accept()` in a loop. Every time a new client connects, it shoves that client's file descriptor (fd) into a queue and goes back to accepting. (See [kvserver.c](kvserver.c) `main`.)
2. **Worker threads** -- there are N of these (I used 8 in the benchmark). Each one sits in a loop calling `queue_get()` to pull an fd from the queue, then runs `handle_client(fd)` which reads commands and answers them until the client quits or disconnects. (See [kvserver.c](kvserver.c) `worker_thread` and [protocol.c](protocol.c) `handle_client`.)
3. **Sweeper thread** -- there is exactly one. It sleeps for `sweeper_interval_ms`, then walks the hash table and deletes any expired keys. (See [sweeper.c](sweeper.c) `sweeper_thread`.)

The main thread is the **producer** (it makes work). Workers are the **consumers** (they take work). That's the classic producer-consumer pattern.

---

## 3. The data structures

### Hash table (Stage 1)
- An array of buckets. Each bucket is a linked list of `struct entry`. (See [kv.h](kv.h) `struct entry`, `struct table`.)
- I hash the key with djb2, mod by `num_buckets`, and walk the linked list.
- Each entry has: key, value, expire time, next pointer.

### Work queue (Stage 2)
- A bounded ring buffer of int file descriptors. (See [kv.h](kv.h) `struct queue`.)
- Has `head`, `tail`, `count`, `cap`, plus one mutex and two condition variables (`not_full`, `not_empty`).
- Capacity is `2 * num_workers` so workers never run out of work in bursts.

---

## 4. The locks (why each one exists)

### Queue mutex + 2 CVs (Stage 2)
- The mutex protects head/tail/count.
- `not_full` is signaled when a worker takes an fd out (so the main thread can put one in).
- `not_empty` is signaled when main puts an fd in (so a worker can pull one out).
- `queue_put` waits on `not_full` if the queue is full. `queue_get` waits on `not_empty` if it's empty.

### Table rwlock (Stage 3)
- One `pthread_rwlock_t` for the whole hash table.
- `kv_get` takes it as a **read lock** -- many GETs can run at once.
- `kv_put` and `kv_del` take it as a **write lock** -- only one writer at a time, and no readers while a writer holds it.
- The rwlock is what keeps GETs from seeing half-written values.

### Atomics (Stage 3)
- The STATS counters (`hits`, `misses`, `puts`, `dels`, `keys`, `active_conns`) are `atomic_long`. I just use `atomic_fetch_add` -- no mutex needed.

---

## 5. Lifecycle of one client request

A grader could ask "trace what happens when a client sends `GET color`." Here's the path:

1. Client opens a TCP connection to port 9000.
2. **Main thread** accepts it, gets an fd, calls `queue_put(fd)`.
3. `queue_put` locks the queue mutex, waits on `not_full` if needed, drops the fd in the ring, signals `not_empty`, unlocks.
4. **A worker** is sleeping on `not_empty` inside `queue_get`. It wakes, takes the fd, signals `not_full`, unlocks.
5. The worker runs `handle_client(fd)`, which loops reading lines.
6. It reads `GET color\n`, sees it starts with `GET `, parses the key.
7. Calls `kv_get("color", out, sizeof(out))`.
8. `kv_get` takes the rwlock as a **read lock**, hashes "color", walks the bucket's linked list, finds the entry, checks `expire`, copies the value, releases the lock, increments `hits` atomically, returns 0.
9. Worker writes `VALUE red\n` back to the socket via `dprintf`.
10. Loop continues until the client sends `QUIT` or disconnects.

---

## 6. Stage by stage -- what each one adds

**Stage 1** -- one client at a time. Just the listen socket, accept loop, hash table, and the protocol parser.

**Stage 2** -- thread pool. Main thread now hands fds to a queue instead of serving them itself. Workers serve in parallel. Need queue mutex + CVs for the producer-consumer pattern. Hash table is still NOT thread-safe yet.

**Stage 3** -- rwlock. Now the hash table is safe under concurrent workers. GETs run in parallel, PUTs/DELs serialize. STATS counters become atomic.

**Stage 4** -- TTL + sweeper. Each entry can have an expire time. `kv_get` treats expired entries as misses. A new sweeper thread wakes every `sweeper_interval_ms`, takes the rwlock per-bucket, and deletes expired entries.

---

## 7. The three design questions (likely viva questions)

### Q: Why one rwlock for the whole table, not per-bucket?
- Simpler. Fewer locks to track, no deadlock risk between buckets.
- Per-bucket would only help **writes**, since reads with one rwlock already run in parallel.
- My benchmark is 90% reads, so per-bucket would not help much on this workload.
- Numbers: 1->4 clients scaled ~3.8x (reads parallelized fine). The plateau at 16+ clients is from the 10% PUTs serializing the whole table -- per-bucket locks WOULD help here, but I traded it for simpler code.

### Q: Worker pool size -- where does it stop helping?
- I tested 2/4/8/16 workers at 16 clients, 90% reads.
- All four gave roughly the same throughput (185-205k ops/sec).
- So adding workers past 2-4 stopped helping.
- Reason: at 90% reads with one rwlock, the bottleneck is CPU/lock, not the number of workers. Extra workers just add scheduler overhead.

### Q: How does the sweeper avoid freezing the server, and avoid races with GETs?
- It locks **one bucket at a time**, not the whole table. So a GET on bucket 5 only blocks if the sweeper is exactly on bucket 5 right now.
- If I locked the whole table during a sweep, all GETs on every bucket would wait until the sweep finished.
- Race safety: GETs use a read lock, sweeper uses a write lock. They cannot both be on the same bucket at once.
- Belt-and-suspenders: `kv_get` also checks `e->expire` directly, so even if the sweeper hasn't gotten there yet, an expired key returns NOT_FOUND.

---

## 8. Things I should be ready to point at in the code

The grader picks 1-2 pieces of code to walk through. Most likely candidates:

- **`kv_get`** in [table.c](table.c) -- read lock, hash, walk chain, expire check, unlock.
- **`kv_put`** in [table.c](table.c) -- write lock, update-in-place vs new node.
- **`queue_put` / `queue_get`** in [queue.c](queue.c) -- the producer-consumer pattern with two CVs.
- **`worker_thread`** in [kvserver.c](kvserver.c) -- the simple consumer loop.
- **`sweeper_thread`** in [sweeper.c](sweeper.c) -- per-bucket locking.
- **`main`** in [kvserver.c](kvserver.c) -- initialization order, accept loop, shutdown.

For each, be ready to say in plain words: what it does, what locks it takes, why.

---

## 9. Quick demo plan for the viva

Per spec: "start it, show concurrent clients, show STATS, show TTL expiry."

1. `make all bench`
2. Terminal 1: `./kvserver 9000 8 1024 500`
3. Terminal 2: `./test_client.sh 9000` -- shows PUT/GET/DEL/STATS/TTL.
4. Terminal 2: `./test_stages.sh 9000` -- shows parallel clients (Stage 2), shared-key contention (Stage 3), TTL sweeper (Stage 4 -- watch keys go from 1043 down to 1023 after the 3s sleep).
5. Terminal 2: `./bench_client 127.0.0.1 9000 16 10000 90` -- shows the throughput number from BENCHMARK.md.
6. Ctrl-C terminal 1 -- clean shutdown.
