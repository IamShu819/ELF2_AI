#!/usr/bin/env bash
set -euo pipefail

root="$(cd "$(dirname "${BASH_SOURCE[0]}")/.." && pwd)"
target="${1:-native}"

if [[ "$target" == "native" ]]; then
  case "$(uname -m)" in
    x86_64|amd64) target="x86_64" ;;
    aarch64|arm64) target="aarch64" ;;
    armv7l|armv7*|armhf) target="armhf" ;;
    *)
      echo "Unsupported native machine: $(uname -m)" >&2
      echo "Use one of: x86_64, aarch64, armhf" >&2
      exit 1
      ;;
  esac
fi

goarm=""
case "$target" in
  x86_64|amd64)
    goarch="amd64"
    lib_triple="x86_64-unknown-linux-gnu"
    default_cc="gcc"
    out_name="linux-x86_64"
    ;;
  aarch64|arm64)
    goarch="arm64"
    lib_triple="aarch64-unknown-linux-gnu"
    default_cc="aarch64-linux-gnu-gcc"
    out_name="linux-aarch64"
    ;;
  armhf|armv7|arm)
    goarch="arm"
    goarm="7"
    lib_triple="arm-unknown-linux-gnueabihf"
    default_cc="arm-linux-gnueabihf-gcc"
    out_name="linux-armhf"
    ;;
  *)
    echo "Unsupported target: $target" >&2
    echo "Use one of: native, x86_64, aarch64, armhf" >&2
    exit 1
    ;;
esac

if [[ "$(uname -s)" == "Linux" ]]; then
  case "$(uname -m):$target" in
    x86_64:x86_64|amd64:x86_64|aarch64:aarch64|arm64:aarch64|armv7*:armhf)
      default_cc="gcc"
      ;;
  esac
fi

cc="${CC:-$default_cc}"
if ! command -v "$cc" >/dev/null 2>&1; then
  echo "C compiler not found: $cc" >&2
  echo "Install the compiler or set CC explicitly, for example:" >&2
  echo "  CC=aarch64-linux-gnu-gcc ./scripts/build_voice_engine_linux.sh aarch64" >&2
  exit 1
fi

pushd "$root" >/dev/null
linux_mod_version="$(go list -m -f '{{.Version}}' github.com/k2-fsa/sherpa-onnx-go-linux)"
gomodcache="$(go env GOMODCACHE)"
lib_dir="$gomodcache/github.com/k2-fsa/sherpa-onnx-go-linux@$linux_mod_version/lib/$lib_triple"
if [[ ! -d "$lib_dir" ]]; then
  echo "Linux sherpa runtime not found: $lib_dir" >&2
  echo "Run: go mod download github.com/k2-fsa/sherpa-onnx-go-linux@$linux_mod_version" >&2
  exit 1
fi

out_dir="${OUT_DIR:-$root/dist/$out_name}"
mkdir -p "$out_dir"

export CGO_ENABLED=1
export GOOS=linux
export GOARCH="$goarch"
export CC="$cc"
if [[ -n "$goarm" ]]; then
  export GOARM="$goarm"
else
  unset GOARM || true
fi

go build -o "$out_dir/voice-engine" .
cp "$lib_dir"/*.so* "$out_dir/"

cat <<EOF
Linux voice engine built:
  target: $target
  output: $out_dir
  cc:     $cc

Deploy the output directory with the models directory, then run:
  LD_LIBRARY_PATH=. ./voice-engine
EOF

popd >/dev/null
