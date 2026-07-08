package rag

import (
	"context"
	"fmt"
	"math"
	"net/http"
	"net/http/httptest"
	"strings"
	"sync/atomic"
	"testing"
	"time"
)

func TestRemoteHTTPEmbedderSuccess(t *testing.T) {
	clientSeen := 0
	server := httptest.NewServer(http.HandlerFunc(func(w http.ResponseWriter, r *http.Request) {
		clientSeen++
		if r.Method != http.MethodPost || r.URL.Path != "/v1/embeddings" {
			t.Fatalf("unexpected request %s %s", r.Method, r.URL.Path)
		}
		body := make([]byte, r.ContentLength)
		_, _ = r.Body.Read(body)
		text := string(body)
		if !strings.Contains(text, `"model":"bge-small-zh-v1.5"`) || !strings.Contains(text, `"input":["`) {
			t.Fatalf("unexpected body %s", text)
		}
		fmt.Fprintf(w, `{"data":[{"embedding":%s}],"model":"bge-small-zh-v1.5"}`, vectorJSON(RemoteEmbeddingDimension, 0.1))
	}))
	defer server.Close()
	embedder, err := NewRemoteHTTPEmbedder(server.URL+"/v1/embeddings", DefaultRemoteEmbeddingModel, server.Client())
	if err != nil {
		t.Fatal(err)
	}
	vec, err := embedder.Embed(context.Background(), "灭火器怎么使用")
	if err != nil {
		t.Fatalf("Embed returned error: %v", err)
	}
	if len(vec) != RemoteEmbeddingDimension {
		t.Fatalf("len(vec)=%d", len(vec))
	}
	if clientSeen != 1 {
		t.Fatalf("requests=%d, want 1", clientSeen)
	}
}

func TestRemoteHTTPEmbedderValidationErrors(t *testing.T) {
	cases := []struct {
		name string
		body string
		want string
	}{
		{"bad json", `{`, "decode"},
		{"data count zero", `{"data":[]}`, "data count 0"},
		{"data count two", fmt.Sprintf(`{"data":[{"embedding":%s},{"embedding":%s}]}`, vectorJSON(RemoteEmbeddingDimension, 0.1), vectorJSON(RemoteEmbeddingDimension, 0.2)), "data count 2"},
		{"empty vector", `{"data":[{"embedding":[]}]}`, "dimension 0"},
		{"wrong dimension", `{"data":[{"embedding":[1,2]}]}`, "dimension 2"},
		{"nan", fmt.Sprintf(`{"data":[{"embedding":%s}]}`, vectorJSONWithValue(RemoteEmbeddingDimension, "NaN")), "decode"},
	}
	for _, tc := range cases {
		t.Run(tc.name, func(t *testing.T) {
			server := httptest.NewServer(http.HandlerFunc(func(w http.ResponseWriter, r *http.Request) { _, _ = w.Write([]byte(tc.body)) }))
			defer server.Close()
			embedder, err := NewRemoteHTTPEmbedder(server.URL, DefaultRemoteEmbeddingModel, server.Client())
			if err != nil {
				t.Fatal(err)
			}
			_, err = embedder.Embed(context.Background(), "text")
			if err == nil || !strings.Contains(err.Error(), tc.want) {
				t.Fatalf("err=%v, want %q", err, tc.want)
			}
		})
	}
}

func TestRemoteHTTPEmbedderRejectsInf(t *testing.T) {
	server := httptest.NewServer(http.HandlerFunc(func(w http.ResponseWriter, r *http.Request) {
		vec := make([]float64, RemoteEmbeddingDimension)
		vec[0] = math.Inf(1)
		fmt.Fprintf(w, `{"data":[{"embedding":[`)
		for i := range vec {
			if i > 0 {
				fmt.Fprint(w, ",")
			}
			if math.IsInf(vec[i], 0) {
				fmt.Fprint(w, "1e999")
			} else {
				fmt.Fprint(w, "0")
			}
		}
		fmt.Fprintf(w, `]}]}`)
	}))
	defer server.Close()
	embedder, err := NewRemoteHTTPEmbedder(server.URL, DefaultRemoteEmbeddingModel, server.Client())
	if err != nil {
		t.Fatal(err)
	}
	_, err = embedder.Embed(context.Background(), "text")
	if err == nil || !strings.Contains(err.Error(), "decode") {
		t.Fatalf("err=%v, want decode invalid number", err)
	}
}

func TestRemoteHTTPEmbedderHTTPStatusAndURLValidation(t *testing.T) {
	for _, raw := range []string{"", "ftp://host/x", "http:///x"} {
		if _, err := NewRemoteHTTPEmbedder(raw, DefaultRemoteEmbeddingModel, nil); err == nil {
			t.Fatalf("NewRemoteHTTPEmbedder(%q) succeeded", raw)
		}
	}
	secretInput := "sensitive text should not appear"
	server := httptest.NewServer(http.HandlerFunc(func(w http.ResponseWriter, r *http.Request) {
		http.Error(w, "server echoes "+secretInput+strings.Repeat("x", maxEmbeddingErrorBytes*2), http.StatusInternalServerError)
	}))
	defer server.Close()
	embedder, err := NewRemoteHTTPEmbedder(server.URL, DefaultRemoteEmbeddingModel, server.Client())
	if err != nil {
		t.Fatal(err)
	}
	_, err = embedder.Embed(context.Background(), secretInput)
	if err == nil || !strings.Contains(err.Error(), "HTTP status 500") {
		t.Fatalf("err=%v, want HTTP status", err)
	}
	if strings.Contains(err.Error(), secretInput) || strings.Contains(err.Error(), "server echoes") {
		t.Fatalf("error leaked untrusted response body/input: %v", err)
	}
}

func TestRemoteHTTPEmbedderContextTimeout(t *testing.T) {
	server := httptest.NewServer(http.HandlerFunc(func(w http.ResponseWriter, r *http.Request) {
		time.Sleep(50 * time.Millisecond)
	}))
	defer server.Close()
	client := server.Client()
	client.Timeout = time.Second
	embedder, err := NewRemoteHTTPEmbedder(server.URL, DefaultRemoteEmbeddingModel, client)
	if err != nil {
		t.Fatal(err)
	}
	ctx, cancel := context.WithTimeout(context.Background(), time.Millisecond)
	defer cancel()
	_, err = embedder.Embed(ctx, "text")
	if err == nil {
		t.Fatal("Embed succeeded, want context timeout")
	}
}

func vectorJSON(n int, value float64) string {
	parts := make([]string, n)
	for i := range parts {
		parts[i] = fmt.Sprintf("%g", value)
	}
	return "[" + strings.Join(parts, ",") + "]"
}

func vectorJSONWithValue(n int, value string) string {
	parts := make([]string, n)
	for i := range parts {
		parts[i] = "0"
	}
	parts[0] = value
	return "[" + strings.Join(parts, ",") + "]"
}

func TestRemoteHTTPEmbedderReusesProvidedHTTPClient(t *testing.T) {
	var requests atomic.Int32
	server := httptest.NewServer(http.HandlerFunc(func(w http.ResponseWriter, r *http.Request) {
		requests.Add(1)
		fmt.Fprintf(w, `{"data":[{"embedding":%s}],"model":"bge-small-zh-v1.5"}`, vectorJSON(RemoteEmbeddingDimension, 0.1))
	}))
	defer server.Close()
	client := server.Client()
	embedder, err := NewRemoteHTTPEmbedder(server.URL, DefaultRemoteEmbeddingModel, client)
	if err != nil {
		t.Fatal(err)
	}
	if embedder.Client != client {
		t.Fatal("embedder did not keep provided http.Client")
	}
	for i := 0; i < 2; i++ {
		if _, err := embedder.Embed(context.Background(), "text"); err != nil {
			t.Fatalf("Embed %d returned error: %v", i, err)
		}
		if embedder.Client != client {
			t.Fatal("embedder replaced provided http.Client")
		}
	}
	if got := requests.Load(); got != 2 {
		t.Fatalf("requests=%d, want 2", got)
	}
}
