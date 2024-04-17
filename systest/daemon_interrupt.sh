#!/bin/bash
set -eux

DATADIR=$(mktemp -d)

# Start the node in daemon mode in the background
$NANO_NODE_EXE --daemon --network dev --data_path $DATADIR &
NODE_PID=$!

# Allow some time for the node to start up completely
sleep 10

kill -SIGINT $NODE_PID

# Poll for the process to terminate
while kill -0 $NODE_PID 2>/dev/null; do
    sleep 1
done

# Check if the process has stopped
if kill -0 $NODE_PID 2>/dev/null; then
    echo "Node did not stop as expected"
    exit 1
else
    echo "Node stopped successfully"
fi
