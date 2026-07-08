# ELF2_AI

ELF2_AI is an embedded smart terminal project that combines a Qt touch interface, a Go voice and IoT backend, local knowledge-base question answering, MQTT cloud communication, serial-device control, map navigation, environment monitoring, SOS reporting, and WebSocket-based call support.

## Overview

This repository contains the deployable source code for the ELF2 AI terminal:

- `qianrushi_Qt`: Qt desktop/touch client for map navigation, voice Q&A, environment monitoring, serial control, and call UI.
- `qianrushi_go`: Go backend for voice interaction, RAG retrieval, MQTT bridge, serial/environment WebSocket services, and cloud command routing.
- Local map assets, road network data, UI assets, startup scripts, and knowledge-base JSON files.

Large runtime artifacts are intentionally not included, such as model weights, ONNX runtime libraries, build outputs, logs, local handoff files, and machine-specific configuration.

## Features

- Localized AI Q&A architecture with voice/text interaction.
- Knowledge-base retrieval augmented generation for park, safety, equipment, and visitor-service questions.
- Environment data ingestion from serial JSON and `/env` WebSocket broadcast.
- MQTT uplink/downlink bridge for telemetry, alarm, and control command transport.
- SOS alarm routing through MQTT `warm` topic.
- Local map tiles and road-network based navigation.
- WebSocket call client for terminal-to-operator voice communication.
- Cross-platform build scripts for host-side and embedded Linux deployment.

## Architecture

```text
+----------------------+
|      Qt Client       |
|  UI / Map / Voice    |
|  Env / Call / Serial |
+----------+-----------+
           |
           | WebSocket: /voice /env
           v
+----------------------+
|      Go Backend      |
| Voice / RAG / MQTT   |
| Env WS / Command Bus |
+------+---------+-----+
       |         |
       |         +-------- MQTT: test / warm / massge
       |
       +-------- Local or remote LLM / Embedding service

Serial device <-> Qt serial owner <-> Go /env WebSocket <-> MQTT cloud side
```

## Repository Layout

```text
ELF2_AI/
|-- qianrushi_Qt/
|   |-- core/              # Navigation, voice client, serial reader, call client
|   |-- pages/             # Main UI pages
|   |-- widgets/           # Reusable Qt widgets
|   |-- assets/            # Icons, mascot, road network
|   |-- tiles/             # Local map tile layout: tiles/{z}/{x}/{y}.png
|   |-- scripts/           # Embedded startup helpers
|   |-- CMakeLists.txt
|   `-- qianrushi.pro
|
`-- qianrushi_go/
    |-- internal/
    |   |-- mqtt/          # MQTT reconnect, publish, subscribe logic
    |   |-- envws/         # Environment WebSocket bridge
    |   |-- websocket/     # Voice WebSocket service
    |   |-- rag/           # Knowledge retrieval and answerability
    |   |-- llm/           # LLM client adapters
    |   |-- asr/ tts/ vad/ # Voice runtime wrappers
    |   `-- serial/        # Optional backend serial reader
    |-- knowledge/         # Local knowledge-base JSON files
    |-- scripts/           # Build and startup scripts
    |-- main.go
    |-- go.mod
    `-- README.md
```

## Main Data Flow

### Environment Upload

```text
Serial JSON -> Qt SerialEnvReader -> /env serial_json -> Go envws -> MQTT topic test
```

### SOS Alarm

```text
Serial {"type":"sos"} -> Qt -> /env -> Go envws -> MQTT topic warm, payload: sos
```

SOS is not replayed after MQTT reconnection if the broker was offline when the alarm occurred.

### Cloud Control Downlink

```text
MQTT topic massge -> Go backend -> /env stm32_command -> Qt -> serial device
```

Control commands are delivered to the serial device and are not converted into local voice announcements.

## Build

### Go Backend

```bash
cd qianrushi_go
go mod download
go test ./...
```

Embedded Linux build:

```bash
cd qianrushi_go
chmod +x ./scripts/*.sh
./scripts/build_voice_engine_linux.sh native
```

### Qt Client

Using qmake:

```bash
mkdir -p build
cd build
qmake CONFIG+=release ../qianrushi_Qt/qianrushi.pro
make -j"$(nproc)"
```

Using CMake where the target Qt environment provides the required modules:

```bash
cmake -S qianrushi_Qt -B build
cmake --build build --config Release
```

Required Qt modules include Widgets, Network, Multimedia, SerialPort, Svg, and OpenGL-related modules used by the project.

## Runtime Configuration

Common backend environment variables:

```bash
VOICE_ADDR=:8080
VOICE_MQTT_BROKER=broker-host-or-ip
VOICE_LLM_BACKEND=http
VOICE_LLM_URL=http://model-host:8000/v1/chat/completions
VOICE_LLM_MODEL=qwen-2.5-7b-instruct
VOICE_RAG_ENABLED=true
VOICE_RAG_EMBEDDING_BACKEND=remote-http
VOICE_RAG_EMBEDDING_URL=http://model-host:8001/v1/embeddings
VOICE_RAG_EMBEDDING_MODEL=bge-small-zh-v1.5
VOICE_SERIAL_READER_ENABLED=false
```

Qt call client variables:

```bash
SMARTNAV_CALL_HOST=call-service-host
SMARTNAV_CALL_PORT=8090
SMARTNAV_CALL_PATH=/call
```

When Qt owns the serial device, keep `VOICE_SERIAL_READER_ENABLED=false` so the backend does not compete for the same serial port.

## Map Data

Local tiles use a standard Web Mercator directory layout:

```text
qianrushi_Qt/tiles/{z}/{x}/{y}.png
```

Road network data is stored in:

```text
qianrushi_Qt/assets/road_network.json
```

The road network loader expects Overpass-style JSON with `way` elements, `tags.highway`, and `geometry` entries containing `lat` and `lon`.

## MQTT Topics

| Topic | Direction | Purpose |
| --- | --- | --- |
| `test` | Device to cloud | Environment telemetry and raw serial JSON |
| `warm` | Device to cloud | Alarm events such as SOS |
| `massge` | Cloud to device | Control commands sent down to the serial device |

## Notes

- Model files and runtime libraries are excluded from this repository. Use the scripts in `qianrushi_go/scripts` to download or prepare them in the deployment environment.
- Build outputs and local IDE state are excluded by `.gitignore`.
- Deployment IP addresses, local credentials, and handoff documents are intentionally not committed.
