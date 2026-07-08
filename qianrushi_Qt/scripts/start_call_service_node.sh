#!/usr/bin/env bash
set -euo pipefail

# The Go backend in qianrushi_go does not provide /call.
# Use this wrapper only when an independent Node/WebSocket call server
# already exists on the Windows/cloud host.
#
# Required environment:
#   CALL_SERVER_DIR=/path/to/node-call-server
# Optional environment:
#   CALL_HOST=0.0.0.0
#   CALL_PORT=8090
#   CALL_PATH=/call
#   CALL_START_COMMAND="npm start"

: "${CALL_SERVER_DIR:?set CALL_SERVER_DIR to the independent Node/WebSocket call server directory}"
export CALL_HOST="${CALL_HOST:-0.0.0.0}"
export CALL_PORT="${CALL_PORT:-8090}"
export CALL_PATH="${CALL_PATH:-/call}"
CALL_START_COMMAND="${CALL_START_COMMAND:-npm start}"

cd "$CALL_SERVER_DIR"
echo "Starting independent /call service: host=$CALL_HOST port=$CALL_PORT path=$CALL_PATH dir=$CALL_SERVER_DIR"
exec bash -lc "$CALL_START_COMMAND"
