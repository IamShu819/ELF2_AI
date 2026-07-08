#!/usr/bin/env bash
set -euo pipefail

# /call is not provided by the Go backend. Start the independent call
# WebSocket service on 192.168.31.116:8090 before launching Qt.
export SMARTNAV_CALL_HOST="${SMARTNAV_CALL_HOST:-192.168.31.116}"
export SMARTNAV_CALL_PORT="${SMARTNAV_CALL_PORT:-8090}"
export SMARTNAV_CALL_PATH="${SMARTNAV_CALL_PATH:-/call}"

exec "${1:-./qianrushi}" "${@:2}"
