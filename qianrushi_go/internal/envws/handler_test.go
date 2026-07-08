package envws

import (
	"encoding/json"
	"net/http/httptest"
	"testing"
	"time"

	"github.com/gorilla/websocket"
)

func TestBroadcastSTM32CommandSendsRawCommandToEnvClient(t *testing.T) {
	h := NewHandler(nil, nil)
	srv := httptest.NewServer(h)
	defer srv.Close()

	conn, _, err := websocket.DefaultDialer.Dial("ws"+srv.URL[len("http"):], nil)
	if err != nil {
		t.Fatalf("dial env websocket: %v", err)
	}
	defer conn.Close()

	command := `{"Target":"SetPusher","Type":"string","Data":"extend","SpeakId":201}`
	if got := h.BroadcastSTM32Command(command); got != 1 {
		t.Fatalf("BroadcastSTM32Command sent=%d, want 1", got)
	}

	_ = conn.SetReadDeadline(time.Now().Add(2 * time.Second))
	var msg map[string]string
	if err := conn.ReadJSON(&msg); err != nil {
		t.Fatalf("read command: %v", err)
	}
	if msg["type"] != "stm32_command" || msg["command"] != command {
		t.Fatalf("message=%v, want raw command", msg)
	}
}

func TestSerialJSONClientMessagePublishesRawPayload(t *testing.T) {
	h := NewHandler(nil, nil)
	published := make(chan []byte, 1)
	h.SetRawPublisher(func(payload []byte) {
		published <- append([]byte(nil), payload...)
	})
	srv := httptest.NewServer(h)
	defer srv.Close()

	conn, _, err := websocket.DefaultDialer.Dial("ws"+srv.URL[len("http"):], nil)
	if err != nil {
		t.Fatalf("dial env websocket: %v", err)
	}
	defer conn.Close()

	raw := `{"Warn":"fire","SpeakId":301}`
	if err := conn.WriteJSON(map[string]string{"type": "serial_json", "payload": raw}); err != nil {
		t.Fatalf("write serial_json: %v", err)
	}

	select {
	case got := <-published:
		if string(got) != raw {
			t.Fatalf("published=%q, want raw %q", got, raw)
		}
	case <-time.After(2 * time.Second):
		t.Fatal("timed out waiting for raw publish")
	}
}

func TestSerialJSONClientMessageRejectsNonObjectJSON(t *testing.T) {
	h := NewHandler(nil, nil)
	published := make(chan []byte, 1)
	h.SetRawPublisher(func(payload []byte) {
		published <- append([]byte(nil), payload...)
	})

	msg, err := json.Marshal(map[string]string{"type": "serial_json", "payload": `[]`})
	if err != nil {
		t.Fatal(err)
	}
	h.handleClientMessage(msg)

	select {
	case got := <-published:
		t.Fatalf("unexpected publish: %q", got)
	case <-time.After(100 * time.Millisecond):
	}
}

func TestSerialJSONSOSPublishesWarmOnly(t *testing.T) {
	h := NewHandler(nil, nil)
	rawPublished := make(chan []byte, 1)
	warmPublished := make(chan string, 1)
	h.SetRawPublisher(func(payload []byte) {
		rawPublished <- append([]byte(nil), payload...)
	})
	h.SetWarmPublisher(func(code string) {
		warmPublished <- code
	})

	msg, err := json.Marshal(map[string]string{"type": "serial_json", "payload": `{"type":"sos"}`})
	if err != nil {
		t.Fatal(err)
	}
	h.handleClientMessage(msg)

	select {
	case got := <-warmPublished:
		if got != "sos" {
			t.Fatalf("warm payload=%q, want sos", got)
		}
	case <-time.After(2 * time.Second):
		t.Fatal("timed out waiting for warm publish")
	}

	select {
	case got := <-rawPublished:
		t.Fatalf("SOS should not publish raw topic test, got %q", got)
	case <-time.After(100 * time.Millisecond):
	}
}

func TestSerialJSONSOSIsCaseInsensitive(t *testing.T) {
	h := NewHandler(nil, nil)
	warmPublished := make(chan string, 1)
	h.SetWarmPublisher(func(code string) {
		warmPublished <- code
	})

	msg, err := json.Marshal(map[string]string{"type": "serial_json", "payload": `{"type":"SOS"}`})
	if err != nil {
		t.Fatal(err)
	}
	h.handleClientMessage(msg)

	select {
	case got := <-warmPublished:
		if got != "sos" {
			t.Fatalf("warm payload=%q, want sos", got)
		}
	case <-time.After(2 * time.Second):
		t.Fatal("timed out waiting for warm publish")
	}
}

func TestSerialJSONSOSSupportsUppercaseTypeKey(t *testing.T) {
	h := NewHandler(nil, nil)
	rawPublished := make(chan []byte, 1)
	warmPublished := make(chan string, 1)
	h.SetRawPublisher(func(payload []byte) {
		rawPublished <- append([]byte(nil), payload...)
	})
	h.SetWarmPublisher(func(code string) {
		warmPublished <- code
	})

	msg, err := json.Marshal(map[string]string{"type": "serial_json", "payload": `{"Type":"SOS"}`})
	if err != nil {
		t.Fatal(err)
	}
	h.handleClientMessage(msg)

	select {
	case got := <-warmPublished:
		if got != "sos" {
			t.Fatalf("warm payload=%q, want sos", got)
		}
	case <-time.After(2 * time.Second):
		t.Fatal("timed out waiting for warm publish")
	}

	select {
	case got := <-rawPublished:
		t.Fatalf("SOS should not publish raw topic test, got %q", got)
	case <-time.After(100 * time.Millisecond):
	}
}

func TestSerialJSONNormalPayloadDoesNotPublishWarm(t *testing.T) {
	h := NewHandler(nil, nil)
	rawPublished := make(chan []byte, 1)
	warmPublished := make(chan string, 1)
	h.SetRawPublisher(func(payload []byte) {
		rawPublished <- append([]byte(nil), payload...)
	})
	h.SetWarmPublisher(func(code string) {
		warmPublished <- code
	})

	raw := `{"Warn":"fire","SpeakId":301}`
	msg, err := json.Marshal(map[string]string{"type": "serial_json", "payload": raw})
	if err != nil {
		t.Fatal(err)
	}
	h.handleClientMessage(msg)

	select {
	case got := <-rawPublished:
		if string(got) != raw {
			t.Fatalf("raw payload=%q, want %q", got, raw)
		}
	case <-time.After(2 * time.Second):
		t.Fatal("timed out waiting for raw publish")
	}

	select {
	case got := <-warmPublished:
		t.Fatalf("normal payload should not publish warm, got %q", got)
	case <-time.After(100 * time.Millisecond):
	}
}

func TestSerialJSONEnvPayloadPushesSnapshotSinkAndRaw(t *testing.T) {
	h := NewHandler(nil, nil)
	rawPublished := make(chan []byte, 1)
	snapshots := make(chan map[string]interface{}, 1)
	h.SetRawPublisher(func(payload []byte) {
		rawPublished <- append([]byte(nil), payload...)
	})
	h.SetEnvSnapshotSink(func(data map[string]interface{}) {
		cp := make(map[string]interface{}, len(data))
		for k, v := range data {
			cp[k] = v
		}
		snapshots <- cp
	})

	raw := `{"Enviroment_Temperation":26.5,"Enviroment_Humidity":61,"Warn":"normal"}`
	msg, err := json.Marshal(map[string]string{"type": "serial_json", "payload": raw})
	if err != nil {
		t.Fatal(err)
	}
	h.handleClientMessage(msg)

	select {
	case got := <-snapshots:
		if got["Enviroment_Temperation"] != 26.5 || got["Enviroment_Humidity"] != float64(61) {
			t.Fatalf("snapshot=%v, want parsed environment fields", got)
		}
	case <-time.After(2 * time.Second):
		t.Fatal("timed out waiting for env snapshot")
	}

	select {
	case got := <-rawPublished:
		if string(got) != raw {
			t.Fatalf("raw payload=%q, want %q", got, raw)
		}
	case <-time.After(2 * time.Second):
		t.Fatal("timed out waiting for raw publish")
	}
}

func TestInitialEnvSnapshotUsesProviderWithoutSerialReader(t *testing.T) {
	h := NewHandler(nil, nil)
	h.SetEnvSnapshotProvider(func() map[string]interface{} {
		return map[string]interface{}{
			"Enviroment_Temperation": 27.5,
			"Enviroment_Humidity":    58.0,
		}
	})
	srv := httptest.NewServer(h)
	defer srv.Close()

	conn, _, err := websocket.DefaultDialer.Dial("ws"+srv.URL[len("http"):], nil)
	if err != nil {
		t.Fatalf("dial env websocket: %v", err)
	}
	defer conn.Close()

	_ = conn.SetReadDeadline(time.Now().Add(2 * time.Second))
	var msg struct {
		Type string                 `json:"type"`
		Data map[string]interface{} `json:"data"`
	}
	if err := conn.ReadJSON(&msg); err != nil {
		t.Fatalf("read initial env snapshot: %v", err)
	}
	if msg.Type != "env_data" {
		t.Fatalf("type=%q, want env_data", msg.Type)
	}
	if msg.Data["Enviroment_Temperation"] != 27.5 || msg.Data["Enviroment_Humidity"] != 58.0 {
		t.Fatalf("data=%v, want provider snapshot", msg.Data)
	}
}
