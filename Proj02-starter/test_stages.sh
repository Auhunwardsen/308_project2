#!/bin/bash
# test_stages.sh -- stage tests for kvserver
# Usage: ./test_stages.sh <port>   (server must already be running)

PORT=${1:-9000}

send() { printf "%s\nQUIT\n" "$1" | nc localhost "$PORT"; }

echo "=== Stage 1: error paths ==="
send "GET"
send "PUT onlykey"
send "FOO bar"
send "DEL ghost"

echo
echo "=== Stage 2: parallel clients ==="
for i in $(seq 1 20); do ( send "PUT k$i v$i"; send "GET k$i" ) & done
wait
send "STATS"

echo
echo "=== Stage 3: shared-key contention ==="
for i in $(seq 1 50); do send "PUT shared$((i % 3)) v$i" & done
wait
send "GET shared0"
send "GET shared1"
send "GET shared2"

echo
echo "=== Stage 4: TTL sweeper ==="
for i in $(seq 1 20); do send "PUT t$i v$i 1"; done
send "STATS"
sleep 3
send "STATS"
