#!/bin/bash
set -eux

DATADIR=$(mktemp -d)

# Start the node in daemon mode in the background
$NANO_NODE_EXE --daemon --network dev --data_path $DATADIR &
NODE_PID=$!

# Allow some time for the node to start up completely
sleep 10

# Send an interrupt signal to the node process
if [[ "$OSTYPE" != "msys" ]]; then
  kill -SIGINT $NODE_PID
else  # For Windows
  taskkill //PID $NODE_PID //F //T
  timeout /t 5 /nobreak > nul
fi



# Check if the process has stopped using a timeout to avoid infinite waiting
if wait $NODE_PID; then
    echo "Node stopped successfully"
else
    echo "Node did not stop as expected"
    exit 1
fi
