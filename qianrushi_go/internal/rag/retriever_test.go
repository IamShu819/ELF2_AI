package rag

import (
	"strings"
	"testing"
)

func TestBuildContextLimitsUnicodeRunes(t *testing.T) {
	results := []Result{
		{Item: KnowledgeItem{ID: "a", Question: "问题一", Answer: strings.Repeat("界", 20)}},
		{Item: KnowledgeItem{ID: "b", Question: "问题二", Answer: strings.Repeat("园", 20)}},
	}
	ctx := BuildContext(results, Config{MaxItemRunes: 12, MaxContextRunes: 20})
	if got, max := len([]rune(ctx)), 20; got > max {
		t.Fatalf("context runes=%d, want <= %d, context=%q", got, max, ctx)
	}
	for _, line := range strings.Split(ctx, "\n") {
		if got, max := len([]rune(line)), 12; got > max {
			t.Fatalf("line runes=%d, want <= %d, line=%q", got, max, line)
		}
	}
}
