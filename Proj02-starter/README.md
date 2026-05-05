# Mini-KV Server

My project 2 for CprE 3080. Its a key-value store that runs over TCP.

## How to build

Just run `make` in this folder.

For the benchmark client too: `make bench or make all bench`

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
I implemented a single pthread_rwlock_t throughout the entire hash table. The kv_get function uses the read lock to permit simultaneous execution of multiple GET operations while the kv_put and kv_del functions use the write lock to restrict access to one writer at any given moment. The main advantage of bucket-level or per-entry locking systems lies in their ability to handle multiple concurrent write operations, whereas the system operates with enhanced simplicity. Two simultaneous PUT operations should work together with different buckets, but the system requires additional programming work and memory space, while lock order management becomes necessary whenever two buckets get accessed simultaneously. The system needs more entry points through which users can access content from the linked-list chains. The rwlock should allow parallel operations because my benchmark consists of 90% read operations, which the data supports through its results. The system achieved 36,058 operations per second with one client and reached 136,751 operations per second with four clients, which represents a performance improvement of approximately 3.8 times. The system performance reaches a maximum of 216,000 operations per second for both 16 and 64 clients, which occurs when 10% of PUT operations cause all operations to run in a serial manner. I selected the straightforward option.

### Worker pool size
I tested 2, 4, 8, and 16 workers all with 16 clients at 90%. The numbers were almost flat: 2 workers got 202,807 ops/sec 4 got 191,750 8 got 185,492 and 16 got 198,987. The throughput remained constant while adding additional workers because the system already used two workers. The reason this happens is that the workers are not actually the bottleneck in this workload. With 90% reads they all sit on the read lock together which is fine but when a PUT comes through the write lock blocks every other worker so only one of them is doing real work in that window. The OS scheduler has to context switch between them which creates overhead that prevents any benefits because the extras have nothing to do after 2 to 4 workers. The workload would require more workers to complete tasks efficiently if it contained more PUT operations or if each request required slow processing time through big values or disk I/O. The system requires only a small pool of workers because it processes fast in-memory operations through a single read-write lock.

### Sweeper
The sweeper operates through a dedicated background thread which activates every sweeper_interval_ms to inspect all buckets for the purpose of deleting outdated records. The essential function operates through bucket write lock acquisition which permits multiple accesses during the entire sweep process to proceed with sweeper operation. The complete sweep operation would result in all server operations getting blocked because I maintained the write lock which needed to remain active until the sweeper completed its work on all 1024 buckets. The process requires handling buckets because this method restricts maximum operation delay to single linked list processing instead of needing to process the entire table. The sweeper maintains safe competition with GET operations because GET employs a read lock while the sweeper utilizes a write lock which prevents simultaneous access to the same bucket. The function kv_get checks e->expire in addition to its main operation because an expired key will return NOT_FOUND when the sweeper has not reached that point yet.
