#!/bin/bash
# test_client.sh -- quick smoke test for kvserver
#
# Usage: ./test_client.sh <port>
# Assumes kvserver is already running on that port.
#
# This is NOT a grading test suite. It is a sanity check to help you verify
# basic protocol conformance before your viva. Write your own more thorough
# tests as you build.


# PROVIDED TEST
# =====================================================================

PORT=${1:-9000}

send() {
    # Send a command and print the reply. Uses nc with a short timeout.
    printf "%s\n" "$1" | nc -q 1 localhost "$PORT"
}

echo "=== Smoke test against localhost:$PORT ==="
echo

echo "--- PUT color red ---"
send "PUT color red"

echo "--- GET color ---"
send "GET color"

echo "--- GET missing ---"
send "GET missing"

echo "--- PUT temp 72 2   (TTL = 2s, Stage 4) ---"
send "PUT temp 72 2"

echo "--- GET temp ---"
send "GET temp"

echo "--- (sleeping 3s for TTL to expire)"
sleep 3

echo "--- GET temp  (expect NOT_FOUND in Stage 4) ---"
send "GET temp"

echo "--- DEL color ---"
send "DEL color"

echo "--- STATS ---"
send "STATS"

# =====================================================================

echo
echo "Starting 4 Stage Testing."


# Stage 1: error paths
# =====================================================================
echo
echo "=== Stage 1: error paths ==="

# TODO: malformed GET (no key)

# TODO: malformed PUT (no value)

# TODO: unknown command

# TODO: DEL a key that doesn't exist

# =====================================================================



# Stage 2: parallel clients
# =====================================================================
echo
echo "=== Stage 2: parallel clients ==="

# TODO: spawn N background clients that each PUT + GET, then `wait`

# TODO: STATS to check the counters

# =====================================================================





# Stage 3: shared-key contention
# =====================================================================
echo
echo "=== Stage 3: shared-key contention ==="

# TODO: many background writers hitting the same few keys, then `wait`

# TODO: GET each shared key, make sure the value isn't garbage

# =====================================================================





# Stage 4: TTL sweeper
# =====================================================================
echo
echo "=== Stage 4: TTL sweeper ==="

# TODO: PUT a bunch of keys with a 1s TTL

# TODO: STATS right away (keys should be high)

# TODO: sleep ~3s so the sweeper runs

# TODO: STATS again (keys should be near 0)

# =====================================================================

echo
echo "Done with 4 Stage Testing."


# =====================================================================
# Helper snippets (just for reference -- copy into the sections above)
# =====================================================================
#
# Send one command:
#   send "GET foo"
#
# About $i, k$i, v$i:
#   $i is the loop counter (1, 2, 3, ...).
#   "k$i" builds a key string -- on iteration 1 it becomes "k1",
#                                on iteration 2 it becomes "k2", etc.
#   "v$i" does the same for the value -- "v1", "v2", "v3", ...
#   So "PUT k$i v$i" sends "PUT k1 v1", "PUT k2 v2", and so on.
#
# Loop 20 times:
#   for i in $(seq 1 20); do send "PUT k$i v$i"; done
#
# Run in background (the & at the end), then wait for them all:
#   send "PUT a 1" &
#   send "PUT b 2" &
#   wait
#
# Sleep for 3 seconds:
#   sleep 3
# =====================================================================
