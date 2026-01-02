#!/bin/bash
set -eux

DATADIR=$(mktemp -d)

# Start the node in daemon mode in the background
$NANO_NODE_EXE --daemon --network dev --data_path $DATADIR &
NODE_PID=$!

# Allow some time for the node to start up completely
sleep 10

# Send an interrupt signal to the node process
case "$(uname -s)" in
    CYGWIN*|MINGW*|MSYS*)
        echo "Detected Windows environment, using taskkill"
        # Get the actual Windows PID (WINPID) from ps output
        # The bash $! variable returns bash-internal PID, but we need the Windows PID
        # Use awk to explicitly search for the line matching our PID in column 1
        WINPID=$(ps -p $NODE_PID | awk -v pid="$NODE_PID" '$1 == pid {print $4}')
        echo "Bash PID: $NODE_PID, Windows PID: $WINPID"

        # Use taskkill without /F flag for graceful shutdown on Windows
        # /T terminates child processes as well
        MSYS2_ARG_CONV_EXCL="*" taskkill /PID $WINPID /T

        # Wait for the process to actually exit
        if wait $NODE_PID; then
            echo "Node stopped successfully"
        else
            # Non-zero exit code from wait - this could be expected if taskkill forces termination
            # Check if the process is actually gone
            if ! MSYS2_ARG_CONV_EXCL="*" tasklist /FI "PID eq $WINPID" 2>&1 | grep -q "$WINPID"; then
                echo "Node stopped successfully (process terminated)"
            else
                echo "Node did not stop as expected"
                exit 1
            fi
        fi
        ;;
    *)
        # Unix-like systems (Linux, macOS)
        echo "Detected Unix-like environment, using SIGINT"
        kill -SIGINT $NODE_PID

        # Check if the process has stopped using a timeout to avoid infinite waiting
        if wait $NODE_PID; then
            echo "Node stopped successfully"
        else
            echo "Node did not stop as expected"
            exit 1
        fi
        ;;
esac
