package rag

import (
	"context"
	"errors"
	"fmt"
	"math"
	"sort"
)

type Store struct {
	entries []entry
}

type entry struct {
	item   KnowledgeItem
	vector []float64
}

type Result struct {
	Item  KnowledgeItem
	Score float64
}

func NewStore(ctx context.Context, items []KnowledgeItem, embedder Embedder) (*Store, error) {
	if embedder == nil {
		return nil, errors.New("embedder is required")
	}
	if err := validateUniqueIDs(items); err != nil {
		return nil, err
	}
	entries := make([]entry, 0, len(items))
	expectedDim := 0
	for _, item := range items {
		if err := item.Validate(); err != nil {
			return nil, err
		}
		vec := append([]float64(nil), item.Embedding...)
		if len(vec) == 0 {
			var err error
			vec, err = embedder.Embed(ctx, item.TextForEmbedding())
			if err != nil {
				return nil, fmt.Errorf("embed knowledge %q: %w", item.ID, err)
			}
		}
		if err := validateVector(fmt.Sprintf("knowledge %q embedding", item.ID), vec); err != nil {
			return nil, err
		}
		if expectedDim == 0 {
			expectedDim = len(vec)
		} else if len(vec) != expectedDim {
			return nil, fmt.Errorf("knowledge %q embedding dimension %d does not match index dimension %d", item.ID, len(vec), expectedDim)
		}
		entries = append(entries, entry{item: item, vector: vec})
	}
	return &Store{entries: entries}, nil
}

func (s *Store) Search(ctx context.Context, query string, embedder Embedder, cfg Config) ([]Result, error) {
	if err := ctx.Err(); err != nil {
		return nil, err
	}
	if s == nil || len(s.entries) == 0 {
		return nil, nil
	}
	if embedder == nil {
		return nil, errors.New("embedder is required")
	}
	cfg = cfg.withDefaults()
	queryVector, err := embedder.Embed(ctx, query)
	if err != nil {
		return nil, err
	}
	if err := validateVector("query embedding", queryVector); err != nil {
		return nil, err
	}
	indexDim := len(s.entries[0].vector)
	if len(queryVector) != indexDim {
		return nil, fmt.Errorf("query embedding dimension %d does not match index dimension %d", len(queryVector), indexDim)
	}

	results := make([]Result, 0, len(s.entries))
	for _, e := range s.entries {
		score := Cosine(queryVector, e.vector)
		if score < cfg.MinScore {
			continue
		}
		results = append(results, Result{Item: e.item, Score: score})
	}
	sort.SliceStable(results, func(i, j int) bool {
		if nearlyEqual(results[i].Score, results[j].Score) {
			return results[i].Item.ID < results[j].Item.ID
		}
		return results[i].Score > results[j].Score
	})
	if len(results) > cfg.TopK {
		results = results[:cfg.TopK]
	}
	return results, nil
}

func Cosine(a, b []float64) float64 {
	if len(a) == 0 || len(b) == 0 || len(a) != len(b) {
		return 0
	}
	var dot, normA, normB float64
	for i := range a {
		dot += a[i] * b[i]
		normA += a[i] * a[i]
		normB += b[i] * b[i]
	}
	if normA == 0 || normB == 0 {
		return 0
	}
	return dot / (math.Sqrt(normA) * math.Sqrt(normB))
}

func nearlyEqual(a, b float64) bool {
	const epsilon = 1e-12
	return math.Abs(a-b) <= epsilon
}

func validateVector(name string, vec []float64) error {
	if len(vec) == 0 {
		return fmt.Errorf("%s is empty", name)
	}
	for i, value := range vec {
		if math.IsNaN(value) || math.IsInf(value, 0) {
			return fmt.Errorf("%s contains invalid value at dimension %d", name, i)
		}
	}
	return nil
}
