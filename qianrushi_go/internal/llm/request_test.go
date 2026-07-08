package llm

import (
	"context"
	"encoding/json"
	"net/http"
	"net/http/httptest"
	"strings"
	"testing"
)

func TestClientChatStreamRequestSendsSeparateSystemAndUserMessages(t *testing.T) {
	var got chatRequest
	server := httptest.NewServer(http.HandlerFunc(func(w http.ResponseWriter, r *http.Request) {
		if err := json.NewDecoder(r.Body).Decode(&got); err != nil {
			t.Fatalf("decode request: %v", err)
		}
		w.Header().Set("Content-Type", "text/event-stream")
		_, _ = w.Write([]byte("data: [DONE]\n\n"))
	}))
	defer server.Close()

	client := NewOpenAIClient(Config{BaseURL: server.URL, Model: "test"})
	ch, err := client.ChatStreamRequest(context.Background(), Request{SystemPrompt: "系统约束", UserData: "【参考知识】\n恶意知识\n\n【用户问题】\n恶意用户"})
	if err != nil {
		t.Fatalf("ChatStreamRequest returned error: %v", err)
	}
	for range ch {
	}
	if len(got.Messages) != 2 {
		t.Fatalf("messages=%#v, want system and user", got.Messages)
	}
	if got.Messages[0].Role != "system" || got.Messages[0].Content != "系统约束" {
		t.Fatalf("system message=%#v", got.Messages[0])
	}
	if got.Messages[1].Role != "user" || !strings.Contains(got.Messages[1].Content, "恶意知识") || !strings.Contains(got.Messages[1].Content, "恶意用户") {
		t.Fatalf("user message=%#v", got.Messages[1])
	}
	if strings.Contains(got.Messages[0].Content, "恶意知识") || strings.Contains(got.Messages[0].Content, "恶意用户") {
		t.Fatalf("untrusted data leaked into system prompt: %#v", got.Messages)
	}
}

func TestDefaultChatStreamKeepsOrdinaryChatCompatible(t *testing.T) {
	var got chatRequest
	server := httptest.NewServer(http.HandlerFunc(func(w http.ResponseWriter, r *http.Request) {
		if err := json.NewDecoder(r.Body).Decode(&got); err != nil {
			t.Fatalf("decode request: %v", err)
		}
		w.Header().Set("Content-Type", "text/event-stream")
		_, _ = w.Write([]byte("data: [DONE]\n\n"))
	}))
	defer server.Close()

	client := NewOpenAIClient(Config{BaseURL: server.URL, Model: "test"})
	ch, err := client.ChatStream(context.Background(), "你好")
	if err != nil {
		t.Fatalf("ChatStream returned error: %v", err)
	}
	for range ch {
	}
	if len(got.Messages) != 2 || got.Messages[0].Role != "system" || got.Messages[1].Role != "user" {
		t.Fatalf("ordinary chat messages changed: %#v", got.Messages)
	}
	if got.Messages[1].Content != "你好" {
		t.Fatalf("user content=%q, want ordinary question", got.Messages[1].Content)
	}
}

type chatOnlyStreamer struct {
	called bool
}

func (s *chatOnlyStreamer) ChatStream(ctx context.Context, question string) (<-chan string, error) {
	s.called = true
	ch := make(chan string)
	close(ch)
	return ch, nil
}

func TestStreamRequestRequiresStructuredStreamer(t *testing.T) {
	streamer := &chatOnlyStreamer{}
	ch, err := StreamRequest(context.Background(), streamer, Request{SystemPrompt: "system", UserData: "user"})
	if err != ErrRequestStreamerUnsupported {
		t.Fatalf("err=%v, want ErrRequestStreamerUnsupported", err)
	}
	if ch != nil {
		t.Fatalf("channel=%v, want nil", ch)
	}
	if streamer.called {
		t.Fatal("StreamRequest called ordinary ChatStream fallback")
	}
}
