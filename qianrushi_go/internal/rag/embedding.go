package rag

import (
	"context"
	"hash/fnv"
	"strings"
	"unicode"
)

// Embedder converts text into a vector. Stage A implementations are
// deterministic test doubles and must not be used to claim semantic retrieval
// quality.
type Embedder interface {
	Embed(ctx context.Context, text string) ([]float64, error)
}

// KeywordEmbedder is a predictable stage-A embedder. Each dimension counts
// whether configured keyword groups appear in the input text.
type KeywordEmbedder struct {
	Dimensions [][]string
}

func NewKeywordEmbedder(dimensions [][]string) KeywordEmbedder {
	copied := make([][]string, len(dimensions))
	for i := range dimensions {
		copied[i] = append([]string(nil), dimensions[i]...)
	}
	return KeywordEmbedder{Dimensions: copied}
}

func (e KeywordEmbedder) Embed(ctx context.Context, text string) ([]float64, error) {
	if err := ctx.Err(); err != nil {
		return nil, err
	}
	lower := strings.ToLower(text)
	vec := make([]float64, len(e.Dimensions))
	for i, group := range e.Dimensions {
		for _, keyword := range group {
			if keyword == "" {
				continue
			}
			if strings.Contains(lower, strings.ToLower(keyword)) {
				vec[i]++
			}
		}
	}
	return vec, nil
}

// HashEmbedder is deterministic and useful when tests need stable vectors for
// arbitrary text. It verifies interfaces and ordering only, not semantic search.
type HashEmbedder struct {
	Dimensions int
}

func (e HashEmbedder) Embed(ctx context.Context, text string) ([]float64, error) {
	if err := ctx.Err(); err != nil {
		return nil, err
	}
	dims := e.Dimensions
	if dims <= 0 {
		dims = 16
	}
	vec := make([]float64, dims)
	for _, token := range strings.FieldsFunc(strings.ToLower(text), func(r rune) bool {
		return unicode.IsSpace(r) || unicode.IsPunct(r) || unicode.IsSymbol(r)
	}) {
		if token == "" {
			continue
		}
		h := fnv.New32a()
		_, _ = h.Write([]byte(token))
		vec[int(h.Sum32())%dims]++
	}
	return vec, nil
}
