#!/usr/bin/env bash
set -euo pipefail

root="$(cd "$(dirname "${BASH_SOURCE[0]}")/.." && pwd)"

export VOICE_ADDR="${VOICE_ADDR:-:8080}"

# Keep /dev/ttySTM32 owned by Qt on LubanCat.
export VOICE_SERIAL_READER_ENABLED="${VOICE_SERIAL_READER_ENABLED:-false}"
export VOICE_SERIAL_PORT="${VOICE_SERIAL_PORT:-/dev/ttySTM32}"
export VOICE_SERIAL_BAUD="${VOICE_SERIAL_BAUD:-115200}"

# Cloud MQTT broker.
export VOICE_MQTT_BROKER="${VOICE_MQTT_BROKER:-192.168.31.68}"
export VOICE_MQTT_PORT="${VOICE_MQTT_PORT:-1883}"
export VOICE_MQTT_USER="${VOICE_MQTT_USER:-}"
export VOICE_MQTT_PASS="${VOICE_MQTT_PASS:-}"

# Remote LLM service on Windows116.
export VOICE_LLM_BACKEND="${VOICE_LLM_BACKEND:-http}"
export VOICE_LLM_URL="${VOICE_LLM_URL:-http://192.168.31.116:8000/v1/chat/completions}"
export VOICE_LLM_MODEL="${VOICE_LLM_MODEL:-qwen-2.5-7b-instruct}"

# Remote RAG embedding service on Windows116.
export VOICE_RAG_ENABLED="${VOICE_RAG_ENABLED:-true}"
export VOICE_RAG_EMBEDDING_BACKEND="${VOICE_RAG_EMBEDDING_BACKEND:-remote-http}"
export VOICE_RAG_EMBEDDING_URL="${VOICE_RAG_EMBEDDING_URL:-http://192.168.31.116:8001/v1/embeddings}"
export VOICE_RAG_EMBEDDING_MODEL="${VOICE_RAG_EMBEDDING_MODEL:-bge-small-zh-v1.5}"

exec "$root/scripts/start_voice_engine_linux.sh" "$@"
