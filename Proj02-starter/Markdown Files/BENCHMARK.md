# Benchmark Results

## Setup

- Machine: <Lab machine>
- Compiler: gcc, `-Wall -Wextra -pthread -O2`
- Server: `./kvserver 9000 8 1024 500`
- Client: `./bench_client 127.0.0.1 9000 <num_clients> 10000 90`
- Workload: 90% GET / 10% PUT, 10,000 ops per client, ~1000-key pool

## How to run

Build:
```
make all bench
```

Terminal 1 (server, leave running):
```
./kvserver 9000 8 1024 500 2>&1 | tee logs/server.txt
```
Expect: `kvserver: listening on port 9000 (workers=8, buckets=1024, sweeper=500ms)`

Terminal 2 (benchmark sweep):
```
for c in 1 4 16 64; do
  ./bench_client 127.0.0.1 9000 $c 10000 90 2>&1 | tee logs/bench_c${c}.txt
done
```

Each run prints wall time and ops/sec -- fills the table below. Ctrl-C terminal 1 when done.

## Results -- Throughput vs. concurrency (90% read / 10% write)

| Clients | Total ops | Wall time (s) | Throughput (ops/sec) |
| ------- | --------- | ------------- | -------------------- |
| 1       | 10,000    | 0.28          | 35,728               |
| 4       | 40,000    | 0.27          | 149,686              |
| 16      | 160,000   | 0.62          | 256,102              |
| 64      | 640,000   | 3.31          | 193,261              |

## Analysis

Throughput scales nearly linearly from 1 to 4 clients (~4.2x). It keeps climbing through 16 clients but only by ~1.7x, then drops at 64. The plateau and regression line up with two effects: writer contention on the single table-wide rwlock from the 10% PUTs, and the worker pool (8) being heavily oversubscribed once client count is much larger. At 90% reads the curve is what you'd expect from one rwlock -- reads parallelize well, writes serialize the whole table.
