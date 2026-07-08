package rag

import (
	"context"
	"fmt"
	"math"
	"net/http"
	"strconv"
	"strings"
	"time"
)

const (
	EnvEnabled            = "VOICE_RAG_ENABLED"
	EnvKnowledgeDir       = "VOICE_RAG_KNOWLEDGE_DIR"
	EnvTopK               = "VOICE_RAG_TOP_K"
	EnvMinScore           = "VOICE_RAG_MIN_SCORE"
	EnvMaxItemRunes       = "VOICE_RAG_MAX_ITEM_RUNES"
	EnvMaxContextRunes    = "VOICE_RAG_MAX_CONTEXT_RUNES"
	EnvEmbeddingBackend   = "VOICE_RAG_EMBEDDING_BACKEND"
	EnvEmbeddingURL       = "VOICE_RAG_EMBEDDING_URL"
	EnvEmbeddingModel     = "VOICE_RAG_EMBEDDING_MODEL"
	EnvEmbeddingTimeoutMS = "VOICE_RAG_EMBEDDING_TIMEOUT_MS"

	DefaultKnowledgeDir     = "./knowledge"
	DefaultEmbeddingBackend = "keyword"
	NoAnswerText            = "这个问题我还需要再了解一下，你可以问我园区服务、安全规范或设备操作方面的问题。"
)

type RuntimeConfig struct {
	Enabled            bool
	KnowledgeDir       string
	EmbeddingBackend   string
	EmbeddingURL       string
	EmbeddingModel     string
	EmbeddingTimeoutMS int
	Retrieval          Config
	Warnings           []string
}

type Runtime struct {
	enabled   bool
	config    RuntimeConfig
	embedder  Embedder
	retriever Retriever
	loadErr   error
}

func DefaultRuntimeConfig() RuntimeConfig {
	return RuntimeConfig{
		Enabled:            true,
		KnowledgeDir:       DefaultKnowledgeDir,
		EmbeddingBackend:   DefaultEmbeddingBackend,
		EmbeddingTimeoutMS: DefaultRemoteEmbeddingTimeoutMS,
		Retrieval:          DefaultConfig(),
	}
}

func ConfigFromEnv(lookup func(string) string) RuntimeConfig {
	if lookup == nil {
		lookup = func(string) string { return "" }
	}
	cfg := DefaultRuntimeConfig()
	if raw := strings.TrimSpace(lookup(EnvEnabled)); raw != "" {
		switch strings.ToLower(raw) {
		case "1", "true", "yes", "on":
			cfg.Enabled = true
		case "0", "false", "no", "off":
			cfg.Enabled = false
		default:
			cfg.Warnings = append(cfg.Warnings, fmt.Sprintf("%s=%q invalid, using %t", EnvEnabled, raw, cfg.Enabled))
		}
	}
	if raw := strings.TrimSpace(lookup(EnvKnowledgeDir)); raw != "" {
		cfg.KnowledgeDir = raw
	}
	if raw := strings.TrimSpace(lookup(EnvEmbeddingBackend)); raw != "" {
		cfg.EmbeddingBackend = strings.ToLower(raw)
	}
	if raw := strings.TrimSpace(lookup(EnvEmbeddingURL)); raw != "" {
		if err := validateRemoteEmbeddingURL(raw); err != nil {
			cfg.Warnings = append(cfg.Warnings, fmt.Sprintf("%s=%q invalid: %v", EnvEmbeddingURL, raw, err))
		} else {
			cfg.EmbeddingURL = raw
		}
	}
	if raw := strings.TrimSpace(lookup(EnvEmbeddingModel)); raw != "" {
		cfg.EmbeddingModel = raw
	}
	cfg.EmbeddingTimeoutMS = parseEnvInt(lookup, EnvEmbeddingTimeoutMS, cfg.EmbeddingTimeoutMS, &cfg.Warnings)
	minScoreRaw := strings.TrimSpace(lookup(EnvMinScore))
	cfg.Retrieval.TopK = parseEnvInt(lookup, EnvTopK, cfg.Retrieval.TopK, &cfg.Warnings)
	cfg.Retrieval.MaxItemRunes = parseEnvInt(lookup, EnvMaxItemRunes, cfg.Retrieval.MaxItemRunes, &cfg.Warnings)
	cfg.Retrieval.MaxContextRunes = parseEnvInt(lookup, EnvMaxContextRunes, cfg.Retrieval.MaxContextRunes, &cfg.Warnings)
	cfg.Retrieval = cfg.Retrieval.withDefaults()
	if minScoreRaw != "" {
		cfg.Retrieval.MinScore = parseEnvMinScore(minScoreRaw, cfg.Retrieval.MinScore, &cfg.Warnings)
		cfg.Retrieval.MinScoreSet = true
	}
	if cfg.EmbeddingBackend == "" {
		cfg.EmbeddingBackend = DefaultEmbeddingBackend
	}
	return cfg
}

func NewRuntime(ctx context.Context, cfg RuntimeConfig) *Runtime {
	cfg = normalizeRuntimeConfig(cfg)
	rt := &Runtime{config: cfg}
	if !cfg.Enabled {
		return rt
	}
	embedder, err := newRuntimeEmbedder(cfg)
	if err != nil {
		rt.loadErr = err
		return rt
	}
	items, err := LoadDir(cfg.KnowledgeDir)
	if err != nil {
		rt.loadErr = err
		return rt
	}
	if cfg.EmbeddingBackend == "remote-http" {
		if err := rejectActiveEmbeddings(items); err != nil {
			rt.loadErr = err
			return rt
		}
	}
	items = enabledKnowledge(items)
	store, err := NewStore(ctx, items, embedder)
	if err != nil {
		rt.loadErr = err
		return rt
	}
	rt.enabled = true
	rt.embedder = embedder
	rt.retriever = NewRetriever(store, embedder, cfg.Retrieval)
	return rt
}

func NewStageBKeywordEmbedder() Embedder {
	return NewKeywordEmbedder([][]string{
		{"安全", "消防", "灭火器", "应急", "烟雾", "火"},
		{"园区", "访客", "访客中心", "服务", "主入口", "登记"},
		{"设备", "巡检终端", "终端", "离线", "网络", "电源", "运维"},
		{"制度", "流程", "手册", "规范", "FAQ", "规定", "操作"},
	})
}

func (r *Runtime) Enabled() bool {
	return r != nil && r.enabled
}

func (r *Runtime) LoadError() error {
	if r == nil {
		return fmt.Errorf("RAG runtime is nil")
	}
	return r.loadErr
}

func (r *Runtime) Retrieve(ctx context.Context, question string) ([]Result, string, bool, error) {
	if !r.Enabled() {
		return nil, "", false, nil
	}
	results, contextText, err := r.retriever.Retrieve(ctx, question)
	if err != nil {
		return nil, "", false, err
	}
	return results, contextText, strings.TrimSpace(contextText) != "", nil
}

func (r *Runtime) RetrieveAnswerable(ctx context.Context, question string) (Answerability, string, bool, error) {
	if !r.Enabled() {
		return Answerability{Answerable: false, DecisionReason: "runtime_disabled"}, "", false, nil
	}
	assessment, contextText, err := r.retriever.RetrieveAnswerable(ctx, question)
	if err != nil {
		return Answerability{}, "", false, err
	}
	return assessment, contextText, assessment.Answerable && strings.TrimSpace(contextText) != "", nil
}

func parseEnvInt(lookup func(string) string, key string, fallback int, warnings *[]string) int {
	raw := strings.TrimSpace(lookup(key))
	if raw == "" {
		return fallback
	}
	value, err := strconv.Atoi(raw)
	if err != nil || value <= 0 {
		*warnings = append(*warnings, fmt.Sprintf("%s=%q invalid, using %d", key, raw, fallback))
		return fallback
	}
	return value
}

func parseEnvMinScore(raw string, fallback float64, warnings *[]string) float64 {
	value, err := strconv.ParseFloat(raw, 64)
	if err != nil || math.IsNaN(value) || math.IsInf(value, 0) || value < -1 || value > 1 {
		*warnings = append(*warnings, fmt.Sprintf("%s=%q invalid, using %.3g", EnvMinScore, raw, fallback))
		return fallback
	}
	return value
}

func normalizeRuntimeConfig(cfg RuntimeConfig) RuntimeConfig {
	defaults := DefaultRuntimeConfig()
	if cfg.KnowledgeDir == "" {
		cfg.KnowledgeDir = defaults.KnowledgeDir
	}
	if cfg.EmbeddingBackend == "" {
		cfg.EmbeddingBackend = defaults.EmbeddingBackend
	}
	if cfg.EmbeddingTimeoutMS <= 0 {
		cfg.EmbeddingTimeoutMS = defaults.EmbeddingTimeoutMS
	}
	cfg.EmbeddingBackend = strings.ToLower(strings.TrimSpace(cfg.EmbeddingBackend))
	cfg.EmbeddingURL = strings.TrimSpace(cfg.EmbeddingURL)
	cfg.EmbeddingModel = strings.TrimSpace(cfg.EmbeddingModel)
	cfg.Retrieval = cfg.Retrieval.withDefaults()
	return cfg
}

func newRuntimeEmbedder(cfg RuntimeConfig) (Embedder, error) {
	switch cfg.EmbeddingBackend {
	case "keyword":
		return NewStageBKeywordEmbedder(), nil
	case "hash":
		return HashEmbedder{Dimensions: 4}, nil
	case "remote-http":
		if cfg.EmbeddingURL == "" {
			return nil, fmt.Errorf("%s is required for remote-http embedding", EnvEmbeddingURL)
		}
		if strings.TrimSpace(cfg.EmbeddingModel) == "" {
			return nil, fmt.Errorf("%s is required for remote-http embedding", EnvEmbeddingModel)
		}
		client := &http.Client{Timeout: time.Duration(cfg.EmbeddingTimeoutMS) * time.Millisecond}
		return NewRemoteHTTPEmbedder(cfg.EmbeddingURL, cfg.EmbeddingModel, client)
	default:
		return nil, fmt.Errorf("unsupported RAG embedding backend %q", cfg.EmbeddingBackend)
	}
}
