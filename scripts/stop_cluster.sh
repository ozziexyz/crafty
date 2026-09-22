#!/bin/bash
set -euo pipefail

cd "$(dirname "$0")/.."

PIDFILE=logs/cluster.pids

if [ ! -f "$PIDFILE" ]; then
    echo "No cluster running (no $PIDFILE)."
    exit 0
fi

while IFS= read -r pid; do
    kill "$pid" 2>/dev/null || true
done < "$PIDFILE"

rm -f "$PIDFILE"
echo "Cluster stopped."
