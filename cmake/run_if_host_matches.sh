#!/usr/bin/env sh
set -eu

if [ "$#" -lt 3 ]; then
    echo "usage: run_if_host_matches.sh <ExpectedHost> <TestLabel> <Command> [args...]" >&2
    exit 2
fi

expected_host="$1"
label="$2"
shift 2

current_host="$(uname -s)"
if [ "$current_host" != "$expected_host" ]; then
    echo "$label: not relevant on $current_host; expected $expected_host"
    exit 77
fi

exec "$@"
