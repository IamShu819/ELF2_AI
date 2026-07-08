package rag

import (
	"bytes"
	"context"
	"encoding/json"
	"fmt"
	"io"
	"math"
	"net/http"
	"net/url"
	"strings"
	"time"
)

const (
	DefaultRemoteEmbeddingModel     = "bge-small-zh-v1.5"
	DefaultRemoteEmbeddingTimeoutMS = 3000
	RemoteEmbeddingDimension        = 512
	maxEmbeddingResponseBytes       = 4 << 20
	maxEmbeddingErrorBytes          = 8 << 10
)

type RemoteHTTPEmbedder struct {
	URL        string
	Model      string
	Client     *http.Client
	Dimensions int
}

type remoteEmbeddingRequest struct {
	Model string   `json:"model"`
	Input []string `json:"input"`
}

type remoteEmbeddingResponse struct {
	Data  []remoteEmbeddingData `json:"data"`
	Model string                `json:"model"`
}

type remoteEmbeddingData struct {
	Embedding []float64 `json:"embedding"`
}

func NewRemoteHTTPEmbedder(endpoint string, model string, client *http.Client) (*RemoteHTTPEmbedder, error) {
	endpoint = strings.TrimSpace(endpoint)
	model = strings.TrimSpace(model)
	if err := validateRemoteEmbeddingURL(endpoint); err != nil {
		return nil, err
	}
	if model == "" {
		return nil, fmt.Errorf("remote embedding model is required")
	}
	if client == nil {
		client = &http.Client{Timeout: time.Duration(DefaultRemoteEmbeddingTimeoutMS) * time.Millisecond}
	}
	return &RemoteHTTPEmbedder{URL: endpoint, Model: model, Client: client, Dimensions: RemoteEmbeddingDimension}, nil
}

func validateRemoteEmbeddingURL(raw string) error {
	if raw == "" {
		return fmt.Errorf("remote embedding URL is required")
	}
	u, err := url.Parse(raw)
	if err != nil {
		return fmt.Errorf("invalid remote embedding URL: %w", err)
	}
	if u.Scheme != "http" && u.Scheme != "https" {
		return fmt.Errorf("remote embedding URL must use http or https")
	}
	if u.Host == "" {
		return fmt.Errorf("remote embedding URL host is required")
	}
	return nil
}

func (e *RemoteHTTPEmbedder) Embed(ctx context.Context, text string) ([]float64, error) {
	if e == nil {
		return nil, fmt.Errorf("remote embedder is nil")
	}
	if e.Client == nil {
		return nil, fmt.Errorf("remote embedder http client is nil")
	}
	payload, err := json.Marshal(remoteEmbeddingRequest{Model: e.Model, Input: []string{text}})
	if err != nil {
		return nil, fmt.Errorf("marshal remote embedding request: %w", err)
	}
	req, err := http.NewRequestWithContext(ctx, http.MethodPost, e.URL, bytes.NewReader(payload))
	if err != nil {
		return nil, fmt.Errorf("create remote embedding request: %w", err)
	}
	req.Header.Set("Content-Type", "application/json")
	resp, err := e.Client.Do(req)
	if err != nil {
		return nil, fmt.Errorf("remote embedding request failed: %w", err)
	}
	defer resp.Body.Close()
	if resp.StatusCode < 200 || resp.StatusCode >= 300 {
		_, _ = io.Copy(io.Discard, io.LimitReader(resp.Body, maxEmbeddingErrorBytes))
		return nil, fmt.Errorf("remote embedding HTTP status %d", resp.StatusCode)
	}
	var decoded remoteEmbeddingResponse
	if err := json.NewDecoder(io.LimitReader(resp.Body, maxEmbeddingResponseBytes)).Decode(&decoded); err != nil {
		return nil, fmt.Errorf("decode remote embedding response: %w", err)
	}
	if len(decoded.Data) != 1 {
		return nil, fmt.Errorf("remote embedding data count %d, want 1", len(decoded.Data))
	}
	vec := append([]float64(nil), decoded.Data[0].Embedding...)
	if len(vec) != e.dimension() {
		return nil, fmt.Errorf("remote embedding dimension %d, want %d", len(vec), e.dimension())
	}
	if err := validateVector("remote embedding", vec); err != nil {
		return nil, err
	}
	for i, value := range vec {
		if math.IsNaN(value) || math.IsInf(value, 0) {
			return nil, fmt.Errorf("remote embedding contains invalid value at dimension %d", i)
		}
	}
	return vec, nil
}

func (e *RemoteHTTPEmbedder) dimension() int {
	if e.Dimensions <= 0 {
		return RemoteEmbeddingDimension
	}
	return e.Dimensions
}
