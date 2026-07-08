package envws

import (
	"bytes"
	"encoding/json"
	"log"
	"net/http"
	"strings"
	"sync"

	"comm-gateway/internal/alert"
	"comm-gateway/internal/serial"

	"github.com/gorilla/websocket"
)

type RawPublisher func(payload []byte)
type WarmPublisher func(code string)
type EnvSnapshotSink func(data map[string]interface{})
type EnvSnapshotProvider func() map[string]interface{}

type Handler struct {
	serialReader  *serial.Reader
	alertMgr      *alert.Manager
	rawPublisher  RawPublisher
	warmPublisher WarmPublisher
	envSink       EnvSnapshotSink
	envProvider   EnvSnapshotProvider
	upgrader      websocket.Upgrader
	clients       map[*websocket.Conn]struct{}
	mu            sync.Mutex
}

func NewHandler(reader *serial.Reader, alertMgr *alert.Manager) *Handler {
	return &Handler{
		serialReader: reader,
		alertMgr:     alertMgr,
		clients:      make(map[*websocket.Conn]struct{}),
		upgrader: websocket.Upgrader{
			CheckOrigin: func(*http.Request) bool { return true },
		},
	}
}

func (h *Handler) SetRawPublisher(fn RawPublisher) {
	h.mu.Lock()
	defer h.mu.Unlock()
	h.rawPublisher = fn
}

func (h *Handler) SetWarmPublisher(fn WarmPublisher) {
	h.mu.Lock()
	defer h.mu.Unlock()
	h.warmPublisher = fn
}

func (h *Handler) SetEnvSnapshotSink(fn EnvSnapshotSink) {
	h.mu.Lock()
	defer h.mu.Unlock()
	h.envSink = fn
}

func (h *Handler) SetEnvSnapshotProvider(fn EnvSnapshotProvider) {
	h.mu.Lock()
	defer h.mu.Unlock()
	h.envProvider = fn
}

func (h *Handler) ServeHTTP(w http.ResponseWriter, r *http.Request) {
	conn, err := h.upgrader.Upgrade(w, r, nil)
	if err != nil {
		log.Printf("[envws] upgrade failed: %v", err)
		return
	}

	h.mu.Lock()
	h.clients[conn] = struct{}{}
	total := len(h.clients)
	h.mu.Unlock()

	log.Printf("[envws] client connected, total=%d", total)

	// Send latest snapshot immediately. Prefer process-level provider
	// populated from Qt serial_json, then fall back to direct serial reader.
	if latest := h.latestSnapshot(); latest != nil {
		if err := conn.WriteJSON(map[string]interface{}{
			"type": "env_data",
			"data": latest,
		}); err != nil {
			log.Printf("[envws] initial env_data write error: %v", err)
			h.removeClient(conn)
			return
		}
	}

	// keep connection alive and handle upstream serial raw JSON from Qt.
	for {
		messageType, payload, err := conn.ReadMessage()
		if err != nil {
			break
		}
		if messageType != websocket.TextMessage {
			continue
		}
		h.handleClientMessage(payload)
	}

	h.removeClient(conn)
}

func (h *Handler) PushData(data map[string]interface{}) {
	h.broadcast(map[string]interface{}{
		"type": "env_data",
		"data": data,
	})
}

func (h *Handler) BroadcastAlarm(ev alert.AlarmEvent) {
	msg := map[string]interface{}{
		"type":    "alarm",
		"code":    ev.Code,
		"active":  ev.Active,
		"message": ev.Message,
	}
	h.broadcast(msg)
}

func (h *Handler) BroadcastCmd(ev alert.CmdEvent) {
	msg := map[string]interface{}{
		"type":    "command",
		"speakId": ev.SpeakId,
		"message": ev.Message,
	}
	h.broadcast(msg)
}

func (h *Handler) BroadcastSTM32Command(command string) int {
	if len(bytes.TrimSpace([]byte(command))) == 0 {
		log.Printf("[envws] empty stm32 command ignored")
		return 0
	}
	n := h.broadcast(map[string]interface{}{
		"type":    "stm32_command",
		"command": command,
	})
	log.Printf("[envws] stm32 command forwarded to %d client(s), bytes=%d", n, len(command))
	return n
}

func (h *Handler) latestSnapshot() map[string]interface{} {
	h.mu.Lock()
	provider := h.envProvider
	h.mu.Unlock()
	if provider != nil {
		if latest := provider(); len(latest) > 0 {
			return latest
		}
	}
	if h.serialReader == nil {
		return nil
	}
	return h.serialReader.Latest()
}

func (h *Handler) handleClientMessage(payload []byte) {
	var msg struct {
		Type    string `json:"type"`
		Payload string `json:"payload"`
	}
	if err := json.Unmarshal(payload, &msg); err != nil {
		log.Printf("[envws] invalid client message ignored: %v", err)
		return
	}
	if msg.Type != "serial_json" {
		return
	}

	raw := bytes.TrimSpace([]byte(msg.Payload))
	log.Printf("[envws] serial_json received, bytes=%d", len(raw))
	if len(raw) == 0 {
		log.Printf("[envws] empty serial_json payload ignored")
		return
	}

	var obj map[string]interface{}
	if err := json.Unmarshal(raw, &obj); err != nil || obj == nil {
		if err != nil {
			log.Printf("[envws] invalid serial_json payload ignored: %v", err)
		} else {
			log.Printf("[envws] serial_json payload is not a JSON object")
		}
		return
	}

	if isSOSPayload(obj) {
		h.mu.Lock()
		publisher := h.warmPublisher
		h.mu.Unlock()
		if publisher == nil {
			log.Printf("[envws] no sos serial_json publisher configured")
			return
		}
		publisher("sos")
		log.Printf("[envws] sos serial_json published to MQTT topic warm")
		if h.alertMgr != nil {
			h.alertMgr.HandleWarm("sos")
		}
		return
	}

	if data := extractEnvData(obj); len(data) > 0 {
		h.PushData(data)
		h.mu.Lock()
		sink := h.envSink
		h.mu.Unlock()
		if sink != nil {
			sink(data)
		}
	}

	h.mu.Lock()
	publisher := h.rawPublisher
	h.mu.Unlock()
	if publisher == nil {
		log.Printf("[envws] no raw serial_json publisher configured")
		return
	}
	publisher(raw)
	log.Printf("[envws] serial_json published raw to MQTT topic test, bytes=%d", len(raw))
}

func isSOSPayload(obj map[string]interface{}) bool {
	for _, key := range []string{"type", "Type", "event", "Event", "Warn", "warn"} {
		value, ok := obj[key]
		if !ok {
			continue
		}
		text, ok := value.(string)
		if ok && strings.EqualFold(strings.TrimSpace(text), "sos") {
			return true
		}
	}
	return false
}

func extractEnvData(obj map[string]interface{}) map[string]interface{} {
	fields := []string{
		"Enviroment_Temperation",
		"Enviroment_Humidity",
		"Enviroment_Light",
		"Enviroment_Pm25",
		"Enviroment_Pm10",
		"Wind_Speed",
		"Wind_Direction",
	}
	data := make(map[string]interface{})
	for _, key := range fields {
		value, ok := obj[key]
		if !ok || value == nil {
			continue
		}
		switch v := value.(type) {
		case float64:
			data[key] = v
		case int, int64, uint64, json.Number:
			data[key] = v
		}
	}
	if len(data) == 0 {
		return nil
	}
	return data
}

func (h *Handler) broadcast(msg interface{}) int {
	h.mu.Lock()
	clients := make([]*websocket.Conn, 0, len(h.clients))
	for c := range h.clients {
		clients = append(clients, c)
	}
	h.mu.Unlock()

	sent := 0
	for _, conn := range clients {
		if err := conn.WriteJSON(msg); err != nil {
			log.Printf("[envws] write error: %v", err)
			h.removeClient(conn)
			continue
		}
		sent++
	}
	return sent
}

func (h *Handler) removeClient(conn *websocket.Conn) {
	h.mu.Lock()
	_, existed := h.clients[conn]
	delete(h.clients, conn)
	total := len(h.clients)
	h.mu.Unlock()
	_ = conn.Close()
	if existed {
		log.Printf("[envws] client disconnected, total=%d", total)
	}
}
