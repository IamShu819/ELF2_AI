package rag

import (
	"context"
	"math"
	"reflect"
	"strings"
	"testing"
)

func TestKeywordEmbedderIsPredictable(t *testing.T) {
	embedder := NewKeywordEmbedder([][]string{{"灭火器", "消防"}, {"访客中心"}})
	vec, err := embedder.Embed(context.Background(), "灭火器和消防检查")
	if err != nil {
		t.Fatalf("Embed returned error: %v", err)
	}
	want := []float64{2, 0}
	if !reflect.DeepEqual(vec, want) {
		t.Fatalf("vec=%v, want %v", vec, want)
	}
}

func TestCosine(t *testing.T) {
	if got := Cosine([]float64{1, 0}, []float64{1, 0}); got != 1 {
		t.Fatalf("same vector cosine=%v, want 1", got)
	}
	if got := Cosine([]float64{1, 0}, []float64{0, 1}); got != 0 {
		t.Fatalf("orthogonal cosine=%v, want 0", got)
	}
	if got := Cosine([]float64{1}, []float64{1, 0}); got != 0 {
		t.Fatalf("mismatched cosine=%v, want 0", got)
	}
}

func TestSearchTopKThresholdAndTieByID(t *testing.T) {
	items := []KnowledgeItem{
		testKnowledge("b", "B", "answer b", []float64{1, 0}),
		testKnowledge("a", "A", "answer a", []float64{1, 0}),
		testKnowledge("c", "C", "answer c", []float64{0, 1}),
	}
	embedder := fixedEmbedder{vec: []float64{1, 0}}
	store, err := NewStore(context.Background(), items, embedder)
	if err != nil {
		t.Fatalf("NewStore returned error: %v", err)
	}

	results, err := store.Search(context.Background(), "query", embedder, Config{TopK: 2, MinScore: 0.5})
	if err != nil {
		t.Fatalf("Search returned error: %v", err)
	}
	gotIDs := resultIDs(results)
	wantIDs := []string{"a", "b"}
	if !reflect.DeepEqual(gotIDs, wantIDs) {
		t.Fatalf("ids=%v, want %v", gotIDs, wantIDs)
	}
}

func TestDefaultConfigValues(t *testing.T) {
	cfg := DefaultConfig()
	if cfg.TopK != 3 {
		t.Fatalf("TopK=%d, want 3", cfg.TopK)
	}
	if cfg.MinScore != 0.55 {
		t.Fatalf("MinScore=%v, want 0.55", cfg.MinScore)
	}
	if cfg.MaxItemRunes != 800 {
		t.Fatalf("MaxItemRunes=%d, want 800", cfg.MaxItemRunes)
	}
	if cfg.MaxContextRunes != 2400 {
		t.Fatalf("MaxContextRunes=%d, want 2400", cfg.MaxContextRunes)
	}
}

func TestSearchDefaultsTopKToThree(t *testing.T) {
	items := []KnowledgeItem{
		testKnowledge("a", "A", "answer a", []float64{1}),
		testKnowledge("b", "B", "answer b", []float64{1}),
		testKnowledge("c", "C", "answer c", []float64{1}),
		testKnowledge("d", "D", "answer d", []float64{1}),
	}
	embedder := fixedEmbedder{vec: []float64{1}}
	store, err := NewStore(context.Background(), items, embedder)
	if err != nil {
		t.Fatalf("NewStore returned error: %v", err)
	}

	results, err := store.Search(context.Background(), "query", embedder, Config{MinScore: 0.5})
	if err != nil {
		t.Fatalf("Search returned error: %v", err)
	}
	if got, want := len(results), DefaultTopK; got != want {
		t.Fatalf("len(results)=%d, want default top-k %d", got, want)
	}
}

func TestSearchEmptyStore(t *testing.T) {
	store, err := NewStore(context.Background(), nil, fixedEmbedder{vec: []float64{1}})
	if err != nil {
		t.Fatalf("NewStore returned error: %v", err)
	}
	results, err := store.Search(context.Background(), "query", fixedEmbedder{vec: []float64{1}}, Config{})
	if err != nil {
		t.Fatalf("Search returned error: %v", err)
	}
	if len(results) != 0 {
		t.Fatalf("len(results)=%d, want 0", len(results))
	}
}

func TestNewStoreRejectsMismatchedKnowledgeDimensions(t *testing.T) {
	items := []KnowledgeItem{
		testKnowledge("a", "A", "answer a", []float64{1, 0}),
		testKnowledge("b", "B", "answer b", []float64{1, 0, 0}),
	}
	_, err := NewStore(context.Background(), items, fixedEmbedder{vec: []float64{1, 0}})
	if err == nil {
		t.Fatal("NewStore succeeded, want dimension error")
	}
	if !strings.Contains(err.Error(), "dimension 3 does not match index dimension 2") {
		t.Fatalf("error=%q, want explicit dimension mismatch", err.Error())
	}
}

func TestSearchRejectsMismatchedQueryDimension(t *testing.T) {
	items := []KnowledgeItem{testKnowledge("a", "A", "answer a", []float64{1, 0})}
	store, err := NewStore(context.Background(), items, fixedEmbedder{vec: []float64{1, 0}})
	if err != nil {
		t.Fatalf("NewStore returned error: %v", err)
	}
	_, err = store.Search(context.Background(), "query", fixedEmbedder{vec: []float64{1, 0, 0}}, Config{})
	if err == nil {
		t.Fatal("Search succeeded, want query dimension error")
	}
	if !strings.Contains(err.Error(), "query embedding dimension 3 does not match index dimension 2") {
		t.Fatalf("error=%q, want explicit query dimension mismatch", err.Error())
	}
}

func TestNewStoreRejectsNaNAndInfKnowledgeVectors(t *testing.T) {
	cases := []struct {
		name string
		vec  []float64
	}{
		{name: "nan", vec: []float64{math.NaN()}},
		{name: "inf", vec: []float64{math.Inf(1)}},
	}
	for _, tc := range cases {
		t.Run(tc.name, func(t *testing.T) {
			items := []KnowledgeItem{testKnowledge(tc.name, "Q", "answer", tc.vec)}
			_, err := NewStore(context.Background(), items, fixedEmbedder{vec: []float64{1}})
			if err == nil {
				t.Fatal("NewStore succeeded, want invalid vector error")
			}
			if !strings.Contains(err.Error(), "contains invalid value") {
				t.Fatalf("error=%q, want invalid value", err.Error())
			}
		})
	}
}

func TestSearchRejectsNaNAndInfQueryVectors(t *testing.T) {
	items := []KnowledgeItem{testKnowledge("a", "A", "answer a", []float64{1})}
	store, err := NewStore(context.Background(), items, fixedEmbedder{vec: []float64{1}})
	if err != nil {
		t.Fatalf("NewStore returned error: %v", err)
	}
	cases := []struct {
		name string
		vec  []float64
	}{
		{name: "nan", vec: []float64{math.NaN()}},
		{name: "inf", vec: []float64{math.Inf(-1)}},
	}
	for _, tc := range cases {
		t.Run(tc.name, func(t *testing.T) {
			_, err := store.Search(context.Background(), "query", fixedEmbedder{vec: tc.vec}, Config{})
			if err == nil {
				t.Fatal("Search succeeded, want invalid query vector error")
			}
			if !strings.Contains(err.Error(), "query embedding contains invalid value") {
				t.Fatalf("error=%q, want invalid query value", err.Error())
			}
		})
	}
}

func resultIDs(results []Result) []string {
	ids := make([]string, len(results))
	for i, result := range results {
		ids[i] = result.Item.ID
	}
	return ids
}

type fixedEmbedder struct {
	vec []float64
}

func (e fixedEmbedder) Embed(context.Context, string) ([]float64, error) {
	return append([]float64(nil), e.vec...), nil
}

func testKnowledge(id, question, answer string, embedding []float64) KnowledgeItem {
	return KnowledgeItem{
		ID:         id,
		Category:   "test",
		Question:   question,
		Answer:     answer,
		Keywords:   []string{question},
		Covers:     []string{CoverFacilityLocation},
		Source:     SourceDemo,
		Enabled:    true,
		EnabledSet: true,
		Embedding:  embedding,
	}
}
