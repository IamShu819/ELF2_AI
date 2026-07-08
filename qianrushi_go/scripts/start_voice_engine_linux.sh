#!/usr/bin/env bash
set -euo pipefail

root="$(cd "$(dirname "${BASH_SOURCE[0]}")/.." && pwd)"

case "$(uname -m)" in
  x86_64|amd64) out_name="linux-x86_64" ;;
  aarch64|arm64) out_name="linux-aarch64" ;;
  armv7l|armv7*|armhf) out_name="linux-armhf" ;;
  *) out_name="" ;;
esac

if [[ -x "$root/voice-engine" ]]; then
  engine="$root/voice-engine"
elif [[ -n "$out_name" && -x "$root/dist/$out_name/voice-engine" ]]; then
  engine="$root/dist/$out_name/voice-engine"
else
  echo "voice-engine binary not found." >&2
  echo "Build it first, for example:" >&2
  echo "  ./scripts/build_voice_engine_linux.sh native" >&2
  exit 1
fi

engine_dir="$(cd "$(dirname "$engine")" && pwd)"
export LD_LIBRARY_PATH="$engine_dir:$root:${LD_LIBRARY_PATH:-}"

cd "$root"
exec "$engine" "$@"
