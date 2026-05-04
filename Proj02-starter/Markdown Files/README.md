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
I just used one rwlock for the whole table. <Fill in: why I did this and not per-bucket.>

### Worker pool size
<Fill in: how many workers helped before it stopped going faster.>

### Sweeper
The sweeper takes the write lock one bucket at a time instead of locking the whole table at once. <Fill in: why, and how GET still works right.>
