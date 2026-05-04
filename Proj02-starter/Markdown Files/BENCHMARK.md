# Benchmark Results

## Setup

- Machine: <Lab machine>
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

## Run the benchmark

Terminal 1 -- start the server, leave it running:
```
./kvserver 9000 8 1024 500
```

You should see a line like:
```
kvserver: listening on port 9000 (workers=8, buckets=1024, sweeper=500ms)
```
If you don't see that, the server didn't start -- check the port isn't in use.

Quick sanity check (terminal 2) before running the benchmark:
```
printf 'PUT k v\nGET k\nQUIT\n' | nc -q 1 localhost 9000
```
Expected: `OK`, `VALUE v`, `BYE`. If that works, the server is up and the protocol is fine.

Terminal 2 -- run the four benchmark cases and save each to its own file:
```
for c in 1 4 16 64; do
  ./bench_client 127.0.0.1 9000 $c 10000 90 | tee bench_c${c}.txt
done
```

Each run prints wall-clock time and ops/sec -- those are the numbers for the table below.

Ctrl-C terminal 1 when done.

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
