#!/usr/bin/env bash
# Phase 1.5 integration tests.
#
# Each test launches ./webserv with a fixture config, captures stdout+stderr,
# and asserts exit code and (optionally) a log substring. We launch with a
# short sleep + kill for the "happy path" cases because ./webserv normally
# runs forever; we only want to confirm it got to "listening" state.
#
# Run from the project root: ./tests/phase15_tests.sh

set -u
PASS=0
FAIL=0
FAILED_TESTS=()

assert_exits_nonzero_with() {
    local name=$1
    local config=$2
    local expect_substr=$3

    local out
    out=$(./webserv "$config" 2>&1)
    local rc=$?
    if [ $rc -eq 0 ]; then
        FAIL=$((FAIL+1)); FAILED_TESTS+=("$name: exit code was 0, expected non-zero")
        return
    fi
    if ! echo "$out" | grep -qF "$expect_substr"; then
        FAIL=$((FAIL+1))
        FAILED_TESTS+=("$name: missing substring '$expect_substr' in output:
$out")
        return
    fi
    PASS=$((PASS+1))
}

assert_listens_on_ports() {
    local name=$1
    local config=$2
    shift 2
    local ports=("$@")

    ./webserv "$config" >/tmp/webserv.out 2>&1 &
    local pid=$!
    sleep 1

    if ! kill -0 "$pid" 2>/dev/null; then
        FAIL=$((FAIL+1))
        FAILED_TESTS+=("$name: webserv died at startup. Output:
$(cat /tmp/webserv.out)")
        return
    fi

    local missing=()
    for port in "${ports[@]}"; do
        if ! lsof -iTCP:"$port" -sTCP:LISTEN -P -n 2>/dev/null | grep -q webserv; then
            missing+=("$port")
        fi
    done

    kill -INT "$pid" 2>/dev/null
    wait "$pid" 2>/dev/null

    if [ ${#missing[@]} -gt 0 ]; then
        FAIL=$((FAIL+1))
        FAILED_TESTS+=("$name: did not listen on ports: ${missing[*]}")
        return
    fi
    PASS=$((PASS+1))
}

# Sanity: binary exists
if [ ! -x ./webserv ]; then
    echo "fatal: ./webserv missing — run 'make' first"
    exit 2
fi

# --- tests appear here, added per task ---

echo
echo "Phase 1.5: $PASS passed, $FAIL failed"
if [ $FAIL -gt 0 ]; then
    printf '%s\n' "${FAILED_TESTS[@]}"
    exit 1
fi
