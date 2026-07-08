# 通话 /call 服务说明

当前 `qianrushi_go` 后端没有 `/call` 路由。Qt 通话页连接的是独立 WebSocket 通话服务。

## Qt 鲁班猫启动环境

```bash
export SMARTNAV_CALL_HOST=192.168.31.68
export SMARTNAV_CALL_PORT=8090
export SMARTNAV_CALL_PATH=/call
./qianrushi
```

也可使用：

```bash
qianrushi_Qt/scripts/start_call_client_lubancat.sh ./qianrushi
```

## 独立 Node/WebSocket 通话服务

如果 `/call` 由独立 Node/WebSocket server 提供，先在服务所在主机启动：

```bash
export CALL_SERVER_DIR=/path/to/node-call-server
export CALL_HOST=0.0.0.0
export CALL_PORT=8090
export CALL_PATH=/call
export CALL_START_COMMAND="npm start"
qianrushi_Qt/scripts/start_call_service_node.sh
```

要求服务端在配对工作人员后发送以下任一 JSON text 事件，Qt 才会进入“通话中”并开始采集麦克风：

```json
{"type":"operator_joined"}
{"type":"paired"}
{"type":"call_ready"}
```

## Audio frame contract for the 68 /call service

The cloud call service at `192.168.31.68:8090/call` must send audio to LubanCat as:

- PCM16 little-endian
- 16000 Hz
- mono
- 20 ms per frame
- exactly 640 bytes per WebSocket binary message

Do not burst large merged audio packets to the device. Each connection should have its own outbound queue capped to 200-500 ms. If the LubanCat client consumes slowly, drop old queued audio frames and keep the newest frames instead of blocking the Node/WebSocket service.

Qt currently accepts a temporary multi-frame binary message only by splitting it into 640-byte frames and logs a warning. Non-640-byte, non-multiple binary audio payloads are dropped and logged.
