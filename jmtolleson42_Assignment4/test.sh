#!/usr/bin/env bash
set -e

# Simple test to demonstrate the server + writer + reader behavior.
# Builds the binaries, starts the server, runs a writer that holds the lock for 5s,
# then starts a reader that will block until the writer releases the lock.



# Start server in background
./lock_server &
SERVER_PID=$!
echo "Started lock_server (pid=$SERVER_PID)"
sleep 1

# Start writer: writes after sleeping 5 seconds while holding the lock
./lock_client file_A WRITE "Hello Distributed World" 5 &
WRITER_PID=$!
echo "Started writer (pid=$WRITER_PID)"

# Give writer a small head start so it acquires lock
sleep 1

# Start reader: it will block until writer unlocks
./lock_client file_A READ &
READER_PID=$!
echo "Started reader (pid=$READER_PID)"

# Wait for processes
wait $WRITER_PID
wait $READER_PID

# Kill server
kill $SERVER_PID || true
echo "Test complete."