package llm

import (
	"context"
	"errors"
	"strings"
)

// Request carries a structured LLM prompt. SystemPrompt and UserData are kept
// separate so RAG can pass policy and untrusted retrieved/user text in distinct
// message roles.
type Request struct {
	SystemPrompt string
	UserData     string
}

type RequestStreamer interface {
	ChatStreamRequest(ctx context.Context, req Request) (<-chan string, error)
}

var ErrRequestStreamerUnsupported = errors.New("llm streamer does not support structured requests")

func NewRequest(systemPrompt string, userData string) Request {
	return Request{SystemPrompt: strings.TrimSpace(systemPrompt), UserData: strings.TrimSpace(userData)}
}

func DefaultRequest(question string) Request {
	return NewRequest(systemPrompt, question)
}

func StreamRequest(ctx context.Context, streamer Streamer, req Request) (<-chan string, error) {
	structured, ok := streamer.(RequestStreamer)
	if !ok {
		return nil, ErrRequestStreamerUnsupported
	}
	return structured.ChatStreamRequest(ctx, req)
}

func (r Request) CombinedPrompt() string {
	system := strings.TrimSpace(r.SystemPrompt)
	user := strings.TrimSpace(r.UserData)
	if system == "" {
		return user
	}
	if user == "" {
		return system
	}
	return "SYSTEM:\n" + system + "\n\nUSER DATA:\n" + user
}

func SystemPrompt() string {
	return systemPrompt
}
