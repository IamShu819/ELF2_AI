package rag

import (
	"context"
	"fmt"
	"strings"
)

type Retriever struct {
	store    *Store
	embedder Embedder
	config   Config
}

func NewRetriever(store *Store, embedder Embedder, cfg Config) Retriever {
	return Retriever{store: store, embedder: embedder, config: cfg.withDefaults()}
}

func (r Retriever) Retrieve(ctx context.Context, query string) ([]Result, string, error) {
	results, err := r.store.Search(ctx, query, r.embedder, r.config)
	if err != nil {
		return nil, "", err
	}
	return results, BuildContext(results, r.config), nil
}

func (r Retriever) RetrieveAnswerable(ctx context.Context, query string) (Answerability, string, error) {
	required, recognized := RequiredCovers(query)
	if !recognized || len(required) == 0 {
		assessment := Answerability{Answerable: false, RequiredCovers: required, DecisionReason: "required_covers_unrecognized"}
		return assessment, "", nil
	}
	results, err := r.store.Search(ctx, query, r.embedder, r.config)
	if err != nil {
		return Answerability{}, "", err
	}
	assessment := EvaluateAnswerability(results, required)
	if !assessment.Answerable {
		return assessment, "", nil
	}
	return assessment, BuildContext(assessment.EligibleResults, r.config), nil
}

func BuildContext(results []Result, cfg Config) string {
	cfg = cfg.withDefaults()
	var b strings.Builder
	for _, result := range results {
		line := fmt.Sprintf("问题：%s。回答：%s", result.Item.Question, result.Item.Answer)
		line = truncateRunes(line, cfg.MaxItemRunes)
		if b.Len() > 0 {
			line = "\n" + line
		}
		if runeLen(b.String())+runeLen(line) > cfg.MaxContextRunes {
			remaining := cfg.MaxContextRunes - runeLen(b.String())
			if remaining <= 0 {
				break
			}
			b.WriteString(truncateRunes(line, remaining))
			break
		}
		b.WriteString(line)
	}
	return b.String()
}

func truncateRunes(s string, limit int) string {
	if limit <= 0 {
		return ""
	}
	runes := []rune(s)
	if len(runes) <= limit {
		return s
	}
	return string(runes[:limit])
}

func runeLen(s string) int {
	return len([]rune(s))
}
