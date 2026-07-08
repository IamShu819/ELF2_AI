# qianrushi_go voice engine

Qt connects to this service through:

```text
ws://127.0.0.1:8080/voice
```

## Runtime dependencies

The voice modules use `sherpa-onnx-go`, so real ASR/TTS/VAD runtime requires:

- `CGO_ENABLED=1`
- A C compiler in `PATH`
  - Windows: MinGW `gcc`
  - Linux native: `gcc`
  - Linux cross compile: for example `aarch64-linux-gnu-gcc`
- sherpa-onnx ASR/TTS/VAD model files
- The local LLM server from `D:\iot\iotcloubs\hubeijinengdasai\shuzi\houduan-D435\llm\start_llm_server.ps1`

Without CGO, the project still compiles, but `/voice` returns a clear startup error when it tries to initialize sherpa.

## Expected model layout

Default paths are relative to this Go project:

```text
models/
├── asr/
│   └── sherpa-onnx-streaming-paraformer-bilingual-zh-en/
│       ├── encoder.int8.onnx
│       ├── decoder.int8.onnx
│       └── tokens.txt
├── tts/
│   └── kokoro-multi-lang-v1_0/
│       ├── model.onnx
│       ├── tokens.txt
│       ├── voices.bin
│       ├── lexicon-zh.txt
│       └── espeak-ng-data/
└── vad/
    └── silero_vad.onnx
```

You can override them:

```powershell
powershell -ExecutionPolicy Bypass -File .\scripts\build_voice_engine.ps1
powershell -ExecutionPolicy Bypass -File .\scripts\start_voice_engine.ps1
```

Linux build scripts are also available:

```bash
chmod +x ./scripts/*.sh
./scripts/download_voice_models_linux.sh
./scripts/build_voice_engine_linux.sh native
./scripts/start_voice_engine_linux.sh
```

Cross build examples:

```bash
./scripts/build_voice_engine_linux.sh x86_64
CC=aarch64-linux-gnu-gcc ./scripts/build_voice_engine_linux.sh aarch64
CC=arm-linux-gnueabihf-gcc ./scripts/build_voice_engine_linux.sh armhf
```

The Linux build output is written to `dist/linux-*` and includes the matching
`libonnxruntime.so` and `libsherpa-onnx-*.so` files from
`sherpa-onnx-go-linux`. When running manually, set the library path:

```bash
LD_LIBRARY_PATH=dist/linux-aarch64 ./dist/linux-aarch64/voice-engine
```

Or run with explicit paths:

```powershell
.\voice-engine.exe `
  --addr :8080 `
  --asr-dir D:\path\to\asr `
  --tts-dir D:\path\to\tts `
  --vad-model D:\path\to\silero_vad.onnx `
  --llm-url http://127.0.0.1:8000/v1/chat/completions
```

## RK3588 local RKLLM backend

On the ELF 2 board, the normal chat LLM can be replaced by a local RKLLM demo
without changing Qt or the sherpa-onnx ASR/TTS/VAD models. The voice pipeline is:

```text
Qt -> /voice -> sherpa ASR -> RKLLM CLI -> sherpa TTS -> Qt playback
```

If the DeepSeek RKLLM demo has already been deployed under `/home/elf/AI`:

```bash
cd /path/to/qianrushi_go
chmod +x ./scripts/*.sh
./scripts/start_voice_engine_rkllm_linux.sh
```

The script sets these defaults:

```bash
VOICE_LLM_BACKEND=rkllm-cli
VOICE_RKLLM_BIN=/home/elf/AI/llm_demo
VOICE_RKLLM_MODEL=/home/elf/AI/DeepSeek-R1-Distill-Qwen-1.5B_W8A8_RK3588.rkllm
VOICE_RKLLM_MAX_NEW_TOKENS=10000
VOICE_RKLLM_MAX_CONTEXT=10000
```

Override them when the model is placed elsewhere:

```bash
VOICE_RKLLM_BIN=/home/elf/AI/llm_demo \
VOICE_RKLLM_MODEL=/home/elf/AI/DeepSeek-R1-Distill-Qwen-1.5B_W8A8_RK3588.rkllm \
./scripts/start_voice_engine_rkllm_linux.sh
```

By default the wrapper removes DeepSeek `<think>...</think>` content before TTS
playback. Set `VOICE_RKLLM_KEEP_THINK=1` only when debugging raw model output.

The default TTS model is `models\tts\vits-melo-tts-zh_en`. Kokoro can be
selected with `VOICE_TTS_ENGINE=kokoro` and
`models\tts\kokoro-multi-lang-v1_0`. Kokoro requires ONNX Runtime 1.24.x; the
build script copies the bundled `runtime\onnxruntime-win-x64-1.24.3\lib`
DLLs into the voice engine directory. Kokoro voice and speed can be adjusted
with `VOICE_TTS_SID` and `VOICE_TTS_SPEED`.

To download the default model set:

```powershell
powershell -ExecutionPolicy Bypass -File .\scripts\download_voice_models.ps1
```

## WebSocket protocol

Qt sends binary audio frames:

- Default input: raw little-endian PCM16, 16 kHz, mono
- Optional input header: `PCM1` + uint32 little-endian sample rate + PCM16 payload
- Realtime input header: `PCM2` + uint32 little-endian sample rate + uint32 little-endian flags + PCM16 payload
  - flag bit 0: Qt is currently playing TTS audio

Server sends JSON text events:

```json
{"type":"state","state":"user_speaking"}
{"type":"asr_partial","text":"..."}
{"type":"asr_final","text":"..."}
{"type":"llm_reply","text":"..."}
{"type":"control","command":"stop"}
{"type":"error","message":"..."}
```

Server sends TTS audio as binary:

```text
TTS1 + uint32 little-endian sample rate + raw little-endian PCM16
```

## MQTT topics and STM32 command downlink

The Go backend owns MQTT access. Qt does not connect to MQTT directly.

- Environment data upload topic: `test`
- STM32 command downlink topic: `massge` (keeps the existing spelling)

Payloads received from `massge` are treated as raw STM32 commands and are
forwarded unchanged through the existing `/voice` WebSocket connection to Qt,
then written to `/dev/ttySTM32` by the Qt serial reader. `SpeakId` is an STM32
command field, not a voice broadcast command on this topic.

Examples:

```json
{"Target":"SetLightGear","Type":"uint","Data":500,"SpeakId":101}
{"Target":"SetPusher","Type":"string","Data":"extend","SpeakId":201}
{"Target":"SetPusher","Type":"string","Data":"retract","SpeakId":202}
{"Target":"SetPusher","Type":"string","Data":"stop","SpeakId":203}
{"Target":"SetPlatform","Type":"string","Data":"up","SpeakId":301}
{"Target":"SetPlatform","Type":"string","Data":"down","SpeakId":302}
{"Target":"SetPlatform","Type":"string","Data":"stop","SpeakId":303}
```
