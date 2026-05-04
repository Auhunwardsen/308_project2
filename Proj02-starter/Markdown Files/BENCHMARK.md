# Benchmark Results

## Setup

- Machine: <CPU model, cores, RAM, OS / kernel>
- Compiler: gcc, `-Wall -Wextra -pthread -O2`
- Server: `./kvserver 9000 8 1024 500`
- Client: `./bench_client 127.0.0.1 9000 <num_clients> 10000 90`
- Workload: 90% GET / 10% PUT, 10,000 ops per client, ~1000-key pool

## How to reproduce

```
make all bench
./kvserver 9000 8 1024 500 &
./bench_client 127.0.0.1 9000 1  10000 90
./bench_client 127.0.0.1 9000 4  10000 90
./bench_client 127.0.0.1 9000 16 10000 90
./bench_client 127.0.0.1 9000 64 10000 90
```

## Save output (one file per run)

Terminal 1 (server, leave running):
```
./kvserver 9000 8 1024 500 | tee server_$(date +%s).log
```

Terminal 2 -- Stage 1 (basic protocol):
```
( printf 'PUT color red\nPUT temp 72 5\nGET color\nGET missing\nSTATS\nQUIT\n'; sleep 1 ) \
  | nc localhost 9000 | tee stage1_$(date +%s).log
```

Terminal 2 -- Stage 2 (many concurrent clients):
```
for i in $(seq 1 20); do
  printf "PUT k$i v$i\nGET k$i\nQUIT\n" | nc localhost 9000 &
done; wait
printf 'STATS\nQUIT\n' | nc localhost 9000 | tee stage2_$(date +%s).log
```

Terminal 2 -- Stage 3 (RW correctness under load):
```
./bench_client 127.0.0.1 9000 16 10000 50 | tee stage3_$(date +%s).log
```

Terminal 2 -- Stage 4 (TTL expiry):
```
( printf 'PUT x hello 2\n'; sleep 3; printf 'GET x\nSTATS\nQUIT\n' ) \
  | nc localhost 9000 | tee stage4_$(date +%s).log
```

Terminal 2 -- benchmark sweep (the table below):
```
for c in 1 4 16 64; do
  ./bench_client 127.0.0.1 9000 $c 10000 90 | tee bench_c${c}_$(date +%s).log
done
```

Terminal 2 -- memory check (valgrind, from spec):
```
valgrind --leak-check=full --show-leak-kinds=all ./kvserver 9000 4 128 2>&1 \
  | tee valgrind_$(date +%s).log
```

Ctrl-C in terminal 1 when done.

## Results -- Throughput vs. concurrency (90% read / 10% write)

| Clients | Total ops | Wall time (s) | Throughput (ops/sec) |
| ------- | --------- | ------------- | -------------------- |
| 1       | 10,000    |               |                      |
| 4       | 40,000    |               |                      |
| 16      | 160,000   |               |                      |
| 64      | 640,000   |               |                      |

## Analysis (3-5 sentences)

<Fill in:
 - Does throughput scale linearly from 1 -> 4 -> 16 clients?
 - Where does it plateau, and roughly by what factor?
 - Best guess at the bottleneck at the plateau: CPU saturation, RW-lock writer
   contention with 10% PUTs, accept loop / queue, syscall overhead, or
   network-loopback bandwidth?
 - Does the curve match what you'd expect for a single table-wide RW lock at
   90% reads?>
