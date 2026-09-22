#!/bin/bash
# Integration test: proves the headline claim — a 5-node cluster elects a
# leader, accepts writes, survives a leader kill without losing acknowledged
# data, accepts the old leader back, and survives a full restart (persistence).
#
# Usage: tests/failover.sh [dir-containing-crafty_node-and-crafty_client]
#   Defaults to ./build relative to the repo root. Wired into `ctest` via
#   CMakeLists.txt, which passes the actual build directory explicitly.

set -uo pipefail

REPO_ROOT="$(cd "$(dirname "${BASH_SOURCE[0]}")/.." && pwd)"
BIN_DIR="${1:-$REPO_ROOT/build}"
NODE_BIN="$BIN_DIR/crafty_node"
CLIENT_BIN="$BIN_DIR/crafty_client"
PORTS=(5550 5551 5552 5553 5554)
NUM_KEYS=20

# Election timeout is 100-250ms; a client call right after a kill can land
# mid-election and fail once before the new leader settles. Retry generously
# instead of tightening assertions — a slow CI runner can need a few passes.
RETRIES=15
RETRY_DELAY=0.3

if [ ! -x "$NODE_BIN" ] || [ ! -x "$CLIENT_BIN" ]; then
    echo "FAIL: crafty_node/crafty_client not found (or not executable) in $BIN_DIR — build the project first" >&2
    exit 1
fi

PASS=0
FAIL=0
pids=()

check() {
    if [ "$1" -eq 0 ]; then
        echo "PASS: $2"
        PASS=$((PASS + 1))
    else
        echo "FAIL: $2"
        FAIL=$((FAIL + 1))
    fi
}

cleanup() {
    for pid in "${pids[@]:-}"; do
        kill -9 "$pid" >/dev/null 2>&1 || true
    done
    rm -rf "$TESTDIR"
}

TESTDIR=$(mktemp -d)
trap cleanup EXIT
cp "$REPO_ROOT/cluster.cfg" "$TESTDIR/"
cd "$TESTDIR" || exit 1
mkdir -p logs

start_node() {
    local port="$1" tag="$2"
    "$NODE_BIN" "$port" > "logs/${port}.${tag}.log" 2>&1 &
    pids+=("$!")
}

start_cluster() {
    local tag="$1"
    pids=()
    for p in "${PORTS[@]}"; do
        start_node "$p" "$tag"
    done
}

kill_all() {
    for pid in "${pids[@]:-}"; do
        kill -9 "$pid" >/dev/null 2>&1 || true
    done
    wait >/dev/null 2>&1 || true
}

# Prints the first port whose log claims leadership, or nothing on timeout.
wait_for_leader() {
    local tag="$1" i p
    for ((i = 0; i < RETRIES * 4; i++)); do
        for p in "${PORTS[@]}"; do
            if grep -q "I'm the leader" "logs/${p}.${tag}.log" 2>/dev/null; then
                echo "$p"
                return 0
            fi
        done
        sleep "$RETRY_DELAY"
    done
    return 1
}

client_put_retry() {
    local key="$1" val="$2" i
    for ((i = 0; i < RETRIES; i++)); do
        if "$CLIENT_BIN" put "$key" "$val" >/dev/null 2>&1; then
            return 0
        fi
        sleep "$RETRY_DELAY"
    done
    return 1
}

# Echoes the value on success, prints nothing and returns 1 on failure.
client_get_retry() {
    local key="$1" out i
    for ((i = 0; i < RETRIES; i++)); do
        out=$("$CLIENT_BIN" get "$key" 2>/dev/null)
        if [ -n "$out" ]; then
            echo "$out"
            return 0
        fi
        sleep "$RETRY_DELAY"
    done
    return 1
}

echo "=== starting 5-node cluster ==="
start_cluster boot
leader=$(wait_for_leader boot)
if [ -z "${leader:-}" ]; then
    check 1 "leader elected on a fresh cluster"
    echo "$FAIL failed, $PASS passed — aborting early, no leader means nothing else can be tested"
    exit 1
fi
check 0 "leader elected on a fresh cluster (node $leader)"

echo "=== putting $NUM_KEYS keys (~100 bytes each — crosses the old 1KB framing limit) ==="
value=$(head -c 100 /dev/zero | tr '\0' 'x')
put_ok=0
for i in $(seq 1 "$NUM_KEYS"); do
    client_put_retry "k$i" "$value" && put_ok=$((put_ok + 1))
done
check $([ "$put_ok" -eq "$NUM_KEYS" ] && echo 0 || echo 1) "all $NUM_KEYS puts acknowledged ($put_ok/$NUM_KEYS)"

echo "=== killing the leader (node $leader) ==="
for i in "${!PORTS[@]}"; do
    [ "${PORTS[$i]}" = "$leader" ] && kill -9 "${pids[$i]}" >/dev/null 2>&1
done

new_leader=""
for ((i = 0; i < RETRIES * 4; i++)); do
    for p in "${PORTS[@]}"; do
        [ "$p" = "$leader" ] && continue
        if grep -q "I'm the leader" "logs/${p}.boot.log" 2>/dev/null; then
            new_leader="$p"
            break 2
        fi
    done
    sleep "$RETRY_DELAY"
done
check $([ -n "$new_leader" ] && echo 0 || echo 1) "a new leader is elected after the kill (node ${new_leader:-none})"

echo "=== verifying all $NUM_KEYS keys are still readable ==="
read_ok=0
for i in $(seq 1 "$NUM_KEYS"); do
    got=$(client_get_retry "k$i") && [ "$got" = "$value" ] && read_ok=$((read_ok + 1))
done
check $([ "$read_ok" -eq "$NUM_KEYS" ] && echo 0 || echo 1) "all $NUM_KEYS keys readable after failover ($read_ok/$NUM_KEYS)"

echo "=== restarting the old leader and confirming the cluster still accepts writes ==="
start_node "$leader" rejoin
client_put_retry "post_rejoin" "ok"
check $? "cluster accepts a write once the old leader rejoins"

echo "=== bonus: kill every node, restart, confirm data survives (persistence) ==="
kill_all
start_cluster restart
restart_leader=$(wait_for_leader restart)
check $([ -n "${restart_leader:-}" ] && echo 0 || echo 1) "a leader is elected after a full cluster restart"

persisted_ok=0
for i in $(seq 1 "$NUM_KEYS"); do
    got=$(client_get_retry "k$i") && [ "$got" = "$value" ] && persisted_ok=$((persisted_ok + 1))
done
check $([ "$persisted_ok" -eq "$NUM_KEYS" ] && echo 0 || echo 1) "all $NUM_KEYS keys survive a full restart ($persisted_ok/$NUM_KEYS)"

kill_all

echo
echo "=== $PASS passed, $FAIL failed ==="
[ "$FAIL" -eq 0 ]
