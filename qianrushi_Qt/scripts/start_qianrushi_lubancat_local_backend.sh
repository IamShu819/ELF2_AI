#!/usr/bin/env bash
set -euo pipefail

export SMARTNAV_BACKEND_HOST="${SMARTNAV_BACKEND_HOST:-127.0.0.1}"
export SMARTNAV_BACKEND_PORT="${SMARTNAV_BACKEND_PORT:-8080}"

# Keep calls connected directly to the cloud /call service.
export SMARTNAV_CALL_HOST="${SMARTNAV_CALL_HOST:-192.168.31.68}"
export SMARTNAV_CALL_PORT="${SMARTNAV_CALL_PORT:-8090}"
export SMARTNAV_CALL_PATH="${SMARTNAV_CALL_PATH:-/call}"

export SMARTNAV_ENV_SERIAL="${SMARTNAV_ENV_SERIAL:-/dev/ttySTM32}"
export SMARTNAV_ENV_BAUD="${SMARTNAV_ENV_BAUD:-115200}"

exec "${1:-./qianrushi}" "${@:2}"
