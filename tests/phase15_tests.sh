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

# Kill any backgrounded ./webserv on script exit (normal, error, or Ctrl+C)
# so a failed run never leaves stray listeners holding ports.
cleanup() {
    pkill -P $$ -f "./webserv" 2>/dev/null || true
}
trap cleanup EXIT INT TERM

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

# Task 1 (deferred): duplicate listen rejected when validator runs through main()
assert_exits_nonzero_with \
    "task1_duplicate_listen_rejected" \
    "config/test_duplicate.conf" \
    "duplicate listen 0.0.0.0:8080"

# Task 4: missing config file
assert_exits_nonzero_with \
    "task4_missing_config" \
    "/tmp/definitely-does-not-exist.conf" \
    "fatal:"

# Task 4: argc > 2 (too many args) — bespoke check because helper takes 1 path arg
out=$(./webserv config/default.conf foo bar 2>&1)
rc=$?
if [ $rc -ne 0 ] && echo "$out" | grep -qF "usage:"; then
    PASS=$((PASS+1))
else
    FAIL=$((FAIL+1))
    FAILED_TESTS+=("task4_too_many_args: rc=$rc, output=$out")
fi

# Task 4: default-path happy case — webserv with no arg listens on 8080
./webserv >/tmp/webserv.out 2>&1 &
PID=$!
sleep 1
if ! kill -0 "$PID" 2>/dev/null; then
    FAIL=$((FAIL+1)); FAILED_TESTS+=("task4_default_path: webserv exited:
$(cat /tmp/webserv.out)")
else
    if lsof -iTCP:8080 -sTCP:LISTEN -P -n 2>/dev/null | grep -q webserv; then
        PASS=$((PASS+1))
    else
        FAIL=$((FAIL+1)); FAILED_TESTS+=("task4_default_path: not listening on 8080")
    fi
    kill -INT "$PID" 2>/dev/null; wait "$PID" 2>/dev/null
fi

# Task 6: one server, two listen directives
assert_listens_on_ports \
    "task6_multi_port_same_server" \
    "config/test_multi_port.conf" \
    8080 8081

# Task 6: two server blocks on distinct ports
assert_listens_on_ports \
    "task6_two_servers" \
    "config/test_two_servers.conf" \
    8090 9090

# --- tests appear here, added per task ---

echo
echo "Phase 1.5: $PASS passed, $FAIL failed"
if [ $FAIL -gt 0 ]; then
    printf '%s\n' "${FAILED_TESTS[@]}"
    exit 1
fi
