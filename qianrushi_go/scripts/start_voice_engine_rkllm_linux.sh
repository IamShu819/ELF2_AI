#!/usr/bin/env bash
set -euo pipefail

root="$(cd "$(dirname "${BASH_SOURCE[0]}")/.." && pwd)"

export VOICE_LLM_BACKEND="${VOICE_LLM_BACKEND:-rkllm-daemon}"
export VOICE_RKLLM_BIN="${VOICE_RKLLM_BIN:-/home/elf/AI/llm_demo}"
export VOICE_RKLLM_MODEL="${VOICE_RKLLM_MODEL:-/home/elf/AI/Qwen2.5-1.5B-Instruct-rk3588-w8a8-opt-1-hybrid-ratio-0.0.rkllm}"
export VOICE_RKLLM_MAX_NEW_TOKENS="${VOICE_RKLLM_MAX_NEW_TOKENS:-256}"
export VOICE_RKLLM_MAX_CONTEXT="${VOICE_RKLLM_MAX_CONTEXT:-2048}"
export VOICE_RKLLM_TIMEOUT_SEC="${VOICE_RKLLM_TIMEOUT_SEC:-90}"

rkllm_dir="$(cd "$(dirname "$VOICE_RKLLM_BIN")" && pwd)"
if [[ -d "$rkllm_dir/lib" ]]; then
  export LD_LIBRARY_PATH="$rkllm_dir/lib:${LD_LIBRARY_PATH:-}"
fi

exec "$root/scripts/start_voice_engine_linux.sh" "$@"
