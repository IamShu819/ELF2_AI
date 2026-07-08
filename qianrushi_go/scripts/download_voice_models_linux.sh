#!/usr/bin/env bash
set -euo pipefail

script_dir="$(cd "$(dirname "${BASH_SOURCE[0]}")" && pwd)"
project_root="$(cd "$script_dir/.." && pwd)"
root="${1:-$project_root/models}"
cache="$root/_downloads"
asr_root="$root/asr"
tts_root="$root/tts"
vad_root="$root/vad"

mkdir -p "$cache" "$asr_root" "$tts_root" "$vad_root"

download_file() {
  local url="$1"
  local destination="$2"
  if [[ -s "$destination" ]]; then
    echo "skip existing archive: $destination"
    return
  fi
  rm -f "$destination"
  echo "download: $url"
  if command -v curl >/dev/null 2>&1; then
    curl -L --fail --retry 3 --retry-delay 2 --connect-timeout 30 -o "$destination" "$url"
  elif command -v wget >/dev/null 2>&1; then
    wget -O "$destination" "$url"
  else
    echo "curl or wget is required" >&2
    exit 1
  fi
  if [[ ! -s "$destination" ]]; then
    echo "download failed or produced an empty file: $destination" >&2
    exit 1
  fi
}

expand_tar_bz2() {
  local archive="$1"
  local destination="$2"
  mkdir -p "$destination"
  echo "extract: $archive -> $destination"
  tar -xjf "$archive" -C "$destination"
}

asr_name="sherpa-onnx-streaming-paraformer-bilingual-zh-en"
asr_archive="$cache/$asr_name.tar.bz2"
asr_dir="$asr_root/$asr_name"
if [[ ! -f "$asr_dir/encoder.int8.onnx" ]]; then
  download_file "https://github.com/k2-fsa/sherpa-onnx/releases/download/asr-models/$asr_name.tar.bz2" "$asr_archive"
  expand_tar_bz2 "$asr_archive" "$asr_root"
else
  echo "skip existing ASR model: $asr_dir"
fi

tts_name="vits-melo-tts-zh_en"
tts_archive="$cache/$tts_name.tar.bz2"
tts_dir="$tts_root/$tts_name"
if [[ ! -f "$tts_dir/model.onnx" ]]; then
  download_file "https://github.com/k2-fsa/sherpa-onnx/releases/download/tts-models/$tts_name.tar.bz2" "$tts_archive"
  expand_tar_bz2 "$tts_archive" "$tts_root"
else
  echo "skip existing TTS model: $tts_dir"
fi

vad_model="$vad_root/silero_vad.onnx"
download_file "https://github.com/k2-fsa/sherpa-onnx/releases/download/asr-models/silero_vad.onnx" "$vad_model"

echo
echo "voice models are ready under: $root"
