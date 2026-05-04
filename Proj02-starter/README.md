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
- Bonus - not done

## Design stuff

### Lock granularity
I just used one rwlock for the whole table. Reads (GET) grab it as a read lock so a bunch can happen at once. Writes (PUT and DEL) grab it as a write lock so only one is allowed at a time. I thought about doing a lock per bucket but it seemed like a lot more code and easier to mess up. Since most of my benchmark is reads (90%), the one big lock seemed fine. Going from 1 to 4 clients was about 3.8x faster which I think means the reads were not really blocking each other.

### Worker pool size
I tried 2, 4, 8, and 16 workers with 16 clients to see what would happen. The numbers came out pretty close, around 185k to 205k ops/sec for all of them. So adding more workers did not really help. I think the slow part is the write lock when PUTs happen, not the workers. After like 2 or 4 workers there is nothing else for them to do.

### Sweeper
The sweeper grabs the write lock one bucket at a time instead of the whole table. If I locked the whole table during a sweep, every GET would have to wait until I was done, which would make the server feel frozen. Doing it one bucket at a time means only that one bucket is locked. GETs are still safe because they use a read lock and the sweeper uses a write lock, so they cant be on the same bucket at the same time. Also kv_get checks the expire time itself, so even if the sweeper has not deleted the key yet, an expired GET still gives NOT_FOUND.
