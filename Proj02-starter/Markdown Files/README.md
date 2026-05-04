# Mini-KV Server

My project 2 for CprE 3080. Its a key-value store that runs over TCP.

## How to build

Just run `make` in this folder.

For the benchmark client too: `make bench`

## How to run

```
./kvserver <port> <num_workers> <num_buckets>
```

Example:
```
./kvserver 9000 8 1024
```

There is an optional 4th arg for the sweeper interval in ms (default 500).

Press Ctrl-C to stop the server.

## How to test

With the server running on port 9000 in another terminal:

```
./test_client.sh 9000          # smoke test (PUT/GET/DEL/STATS/TTL)
./test_stages.sh 9000          # stage 1-4 tests (errors, parallel, contention, sweeper)
```

For benchmarks see BENCHMARK.md.

## What works

- Stage 1 (basic server, GET/PUT/DEL/STATS/QUIT) - done
- Stage 2 (thread pool + queue) - done
- Stage 3 (rwlock on the table) - done
- Stage 4 (TTL sweeper) - done
- Bonus - did not do this one

## Design stuff

### Lock granularity
I used one rwlock for the whole table. GETs take it as a read lock so many can run at the same time, PUT/DEL take it as a write lock so only one can run. Per-bucket locks would let writes on different buckets go in parallel, but they also add a lot of complexity. Since the benchmark is 90% reads, one rwlock was enough -- 1 to 4 clients scaled almost linearly (about 4.2x), which means reads were not blocking each other.

### Worker pool size
I ran the server with 8 workers. Throughput grew with more clients up to 16 (256k ops/sec) but dropped at 64 (193k). Adding workers helps until you have about one worker per CPU core. Past that, extra clients just sit in the queue and the writer lock from PUTs gets hit more often.

### Sweeper
The sweeper takes the write lock one bucket at a time instead of the whole table. If it held the lock across the whole scan, every GET would freeze until the sweep finished. Per-bucket locking means only the bucket being swept is blocked. GETs are still safe because they use a read lock and the sweeper uses a write lock -- they cannot run on the same bucket at the same time. As a backup, `kv_get` also checks the expire time itself, so an expired key returns NOT_FOUND even before the sweeper deletes it.
