# Benchmark Results

## Setup

- Machine: <Lab machine>
- Compiler: gcc, `-Wall -Wextra -pthread -O2`
- Server: `./kvserver 9000 8 1024 500`
- Client: `./bench_client 127.0.0.1 9000 <num_clients> 10000 90`
- Workload: 90% GET / 10% PUT, 10,000 ops per client, ~1000-key pool

## Command formats (from spec)

```
./kvserver    <port> <num_workers> <num_buckets> [sweeper_interval_ms]
./bench_client <host> <port> <num_clients> <ops_per_client> <read_pct>
```

Examples I used:
```
./kvserver 9000 8 1024 500
./bench_client 127.0.0.1 9000 16 10000 90
```

## How to run

chmod +x test_client.sh test_stages.sh `if not done already`

Build:
```
make all bench
```

Terminal 1 (server, leave running):
```
./kvserver 9000 8 1024 500 2>&1 | tee logs/server_$(date +%s).txt
```
Expect: `kvserver: listening on port 9000 (workers=8, buckets=1024, sweeper=500ms)`

Terminal 2 (client sweep -- 1/4/16/64 clients, fixed 8 workers):
```
TS=$(date +%s)
for c in 1 4 16 64; do
  ./bench_client 127.0.0.1 9000 $c 10000 90 2>&1 | tee logs/bench_c${c}_${TS}.txt
done
```

Terminal 2 (worker sweep -- 2/4/8/16 workers, fixed 16 clients; starts/stops server itself):
```
TS=$(date +%s)
for w in 2 4 8 16; do
  ./kvserver 9000 $w 1024 500 >/dev/null 2>&1 &
  PID=$!; sleep 0.5
  ./bench_client 127.0.0.1 9000 16 10000 90 2>&1 | tee logs/workers_w${w}_${TS}.txt
  kill $PID; wait $PID 2>/dev/null
done
```

## Results -- Throughput vs. concurrency (90% read / 10% write, 8 workers)

| Clients | Total ops | Wall time (s) | Throughput (ops/sec) |
| ------- | --------- | ------------- | -------------------- |
| 1       | 10,000    | 0.28          | 36,058               |
| 4       | 40,000    | 0.29          | 136,751              |
| 16      | 160,000   | 0.74          | 215,961              |
| 64      | 640,000   | 2.96          | 216,343              |

## Worker pool sweep (16 clients, 90% read)

| Workers | Throughput (ops/sec) |
| ------- | -------------------- |
| 2       | 202,807              |
| 4       | 191,750              |
| 8       | 185,492              |
| 16      | 198,987              |

## Analysis

Going from 1 to 4 clients was almost 4x faster which I think makes sense because reads can happen at the same time. From 4 to 16 it kept going up but not as much. From 16 to 64 it basically stopped getting faster. I think this is because the PUT lock has to be by itself, so once enough clients are pushing PUTs the lock is busy a lot. The worker sweep shows about the same number (~185-205k) no matter how many workers I used, so the workers are not the slow part -- the lock is.
