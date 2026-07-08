package websocket

import (
	"net/http"
	"net/http/httptest"
	"strings"
	"testing"

	"github.com/gorilla/websocket"
)

func TestBroadcastSTM32CommandNoClientSafe(t *testing.T) {
	h := NewHandler(Config{})
	if got := h.BroadcastSTM32Command("OPEN"); got != 0 {
		t.Fatalf("BroadcastSTM32Command no client = %d, want 0", got)
	}
}

func TestBroadcastSTM32CommandEntersDownlinkPath(t *testing.T) {
	var upgrader = websocket.Upgrader{CheckOrigin: func(*http.Request) bool { return true }}
	serverConnCh := make(chan *websocket.Conn, 1)
	server := httptest.NewServer(http.HandlerFunc(func(w http.ResponseWriter, r *http.Request) {
		conn, err := upgrader.Upgrade(w, r, nil)
		if err != nil {
			t.Errorf("upgrade: %v", err)
			return
		}
		serverConnCh <- conn
	}))
	defer server.Close()

	wsURL := "ws" + strings.TrimPrefix(server.URL, "http")
	clientConn, _, err := websocket.DefaultDialer.Dial(wsURL, nil)
	if err != nil {
		t.Fatalf("dial: %v", err)
	}
	defer clientConn.Close()

	serverConn := <-serverConnCh
	defer serverConn.Close()

	h := NewHandler(Config{})
	s := &session{conn: serverConn}
	h.registerSession(s)
	defer h.unregisterSession(s)

	if got := h.BroadcastSTM32Command("OPEN"); got != 1 {
		t.Fatalf("BroadcastSTM32Command online = %d, want 1", got)
	}

	var ev event
	if err := clientConn.ReadJSON(&ev); err != nil {
		t.Fatalf("read command event: %v", err)
	}
	if ev.Type != "stm32_command" || ev.Command != "OPEN" {
		t.Fatalf("event = %#v, want stm32_command OPEN", ev)
	}
}


func TestBroadcastSTM32CommandPreservesSTM32JSONPayload(t *testing.T) {
	var upgrader = websocket.Upgrader{CheckOrigin: func(*http.Request) bool { return true }}
	serverConnCh := make(chan *websocket.Conn, 1)
	server := httptest.NewServer(http.HandlerFunc(func(w http.ResponseWriter, r *http.Request) {
		conn, err := upgrader.Upgrade(w, r, nil)
		if err != nil {
			t.Errorf("upgrade: %v", err)
			return
		}
		serverConnCh <- conn
	}))
	defer server.Close()

	wsURL := "ws" + strings.TrimPrefix(server.URL, "http")
	clientConn, _, err := websocket.DefaultDialer.Dial(wsURL, nil)
	if err != nil {
		t.Fatalf("dial: %v", err)
	}
	defer clientConn.Close()

	serverConn := <-serverConnCh
	defer serverConn.Close()

	h := NewHandler(Config{})
	s := &session{conn: serverConn}
	h.registerSession(s)
	defer h.unregisterSession(s)

	payload := `{"Target":"SetLightGear","Type":"uint","Data":500,"SpeakId":101}`
	if got := h.BroadcastSTM32Command(payload); got != 1 {
		t.Fatalf("BroadcastSTM32Command online = %d, want 1", got)
	}

	var ev event
	if err := clientConn.ReadJSON(&ev); err != nil {
		t.Fatalf("read command event: %v", err)
	}
	if ev.Type != "stm32_command" || ev.Command != payload {
		t.Fatalf("event = %#v, want raw stm32 JSON payload", ev)
	}
}

func TestSTM32CommandPathDoesNotAffectEnvSnapshot(t *testing.T) {
	store := NewEnvStore()
	s := &session{envStore: store}
	s.setEnvSnapshot(map[string]interface{}{"Enviroment_Temperation": 30.5})

	h := NewHandler(Config{})
	if got := h.BroadcastSTM32Command("OPEN"); got != 0 {
		t.Fatalf("BroadcastSTM32Command no client = %d, want 0", got)
	}

	answer, ok := s.localEnvAnswer("现在温度多少")
	if !ok {
		t.Fatal("local env answer should still work")
	}
	want := "当前环境温度是30.5摄氏度。"
	if answer != want {
		t.Fatalf("localEnvAnswer = %q, want %q", answer, want)
	}
}
