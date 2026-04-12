#!/bin/bash
set -uo pipefail

source "$(dirname "$BASH_SOURCE")/common.sh"

ext=$(get_exec_extension)

echo "Running core_test and rpc_test in parallel"

./core_test${ext} &
core_pid=$!

./rpc_test${ext} &
rpc_pid=$!

core_status=0
rpc_status=0

wait $core_pid || core_status=$?
wait $rpc_pid || rpc_status=$?

echo "Core Test return code: $core_status"
echo "RPC Test return code: $rpc_status"

exit_code=0

if [ $core_status -ne 0 ]; then
    echo "::error::Test failed: core_test"
    exit_code=1
fi

if [ $rpc_status -ne 0 ]; then
    echo "::error::Test failed: rpc_test"
    exit_code=1
fi

exit $exit_code
