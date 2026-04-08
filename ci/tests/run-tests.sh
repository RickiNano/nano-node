#!/bin/bash
set -uo pipefail

source "$(dirname "$BASH_SOURCE")/common.sh"

target=$1
if [ -z "${target-}" ]; then
    echo "Target not specified"
    exit 1
fi

echo "Running tests for target: ${target}"

# Enable core dumps for this process
if [ -n "${COREDUMP_DIR-}" ]; then
    ulimit -c unlimited
fi

# Run the test
shift
executable=./${target}$(get_exec_extension)

if [ -n "${GTEST_WORKERS-}" ]; then
    python3 "$(dirname "$BASH_SOURCE")/../gtest_parallel.py" \
        --workers="${GTEST_WORKERS}" \
        --serialize_test_cases \
        --print_test_times \
        "${executable}" -- "$@"
else
    "${executable}" "$@"
fi
status=$?

if [ $status -ne 0 ]; then
    echo "::error::Test failed: ${target}"

    # Show core dumps if core dump collection is enabled
    if [ -n "${COREDUMP_DIR-}" ]; then
        "$(dirname "$BASH_SOURCE")/show-core-dumps.sh" "${executable}"
    fi

    exit $status
else
    exit 0
fi
