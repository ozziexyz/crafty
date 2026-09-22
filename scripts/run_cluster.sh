#!/bin/bash
set -euo pipefail

cd "$(dirname "$0")/.."

PIDFILE=logs/cluster.pids
PORTS=(5550 5551 5552 5553 5554)

if [ -f "$PIDFILE" ]; then
    while IFS= read -r pid; do
        if kill -0 "$pid" 2>/dev/null; then
            echo "Error: cluster already running (pid $pid is alive)." >&2
            echo "Run scripts/stop_cluster.sh first." >&2
            exit 1
        fi
    done < "$PIDFILE"
fi

mkdir -p logs
: > "$PIDFILE"

for port in "${PORTS[@]}"; do
    ./build/crafty_node "$port" > "logs/node${port}.log" 2>&1 &
    echo $! >> "$PIDFILE"
done

sleep 0.5

failed=0
while IFS= read -r pid; do
    kill -0 "$pid" 2>/dev/null || failed=1
done < "$PIDFILE"

if [ "$failed" -eq 1 ]; then
    echo "Error: one or more nodes failed to start. Check logs/node*.log" >&2
    exit 1
fi

echo "Cluster is up. Writing logs to logs/node*.log"
