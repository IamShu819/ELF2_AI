package rag

import (
	"context"
	"reflect"
	"strings"
	"testing"
)

func TestRequiredCoversRecognizesEvalQuestionTypes(t *testing.T) {
	cases := []struct {
		question string
		want     []string
	}{
		{"园区联系电话是多少", []string{CoverContactPhone}},
		{"服务台今天几点下班", []string{CoverOpeningHours}},
		{"A座三楼 305 房间在哪里", []string{CoverSpecificRoomLocation}},
		{"园区快递柜编号是多少", []string{CoverIdentifier}},
		{"A座三楼 305 房间在哪里，房间编号是多少", []string{CoverSpecificRoomLocation, CoverIdentifier}},
		{"灭火器怎么使用", []string{CoverSafetyGuidance}},
		{"发生火情怎么办", []string{CoverEmergencyGuidance}},
		{"终端怎么开始语音问答", []string{CoverEquipmentOperation}},
		{"这个系统能做什么", []string{CoverCapability}},
	}
	for _, tc := range cases {
		got, ok := RequiredCovers(tc.question)
		if !ok || !reflect.DeepEqual(got, tc.want) {
			t.Fatalf("RequiredCovers(%q)=(%v,%v), want (%v,true)", tc.question, got, ok, tc.want)
		}
	}
}

func TestRequiredCoversDomainUnknownFailsClosed(t *testing.T) {
	covers, ok := RequiredCovers("园区这个情况应该怎么判断")
	if !ok || len(covers) != 0 {
		t.Fatalf("RequiredCovers unknown=(%v,%v), want recognized with empty covers for fail-closed", covers, ok)
	}
	assessment := EvaluateAnswerability(nil, covers)
	if assessment.Answerable || assessment.DecisionReason != "required_covers_unrecognized" {
		t.Fatalf("assessment=%#v, want fail-closed unrecognized", assessment)
	}
}

func TestEvaluateAnswerabilityAllowsJointCoverageAndStableOrder(t *testing.T) {
	results := []Result{
		{Item: KnowledgeItem{ID: "b", Covers: []string{CoverIdentifier}}, Score: 0.91},
		{Item: KnowledgeItem{ID: "a", Covers: []string{CoverSpecificRoomLocation}}, Score: 0.9},
		{Item: KnowledgeItem{ID: "c", Covers: []string{CoverFacilityLocation}}, Score: 0.89},
	}
	assessment := EvaluateAnswerability(results, []string{CoverSpecificRoomLocation, CoverIdentifier})
	if !assessment.Answerable {
		t.Fatalf("assessment answerable=false: %#v", assessment)
	}
	if want := []string{"b", "a"}; !reflect.DeepEqual(assessment.EligibleHitIDs, want) {
		t.Fatalf("eligible ids=%v, want %v", assessment.EligibleHitIDs, want)
	}
}

func TestEvaluateAnswerabilityRejectsMissingOneOfMultipleCovers(t *testing.T) {
	results := []Result{{Item: KnowledgeItem{ID: "room", Covers: []string{CoverSpecificRoomLocation}}, Score: 0.9}}
	assessment := EvaluateAnswerability(results, []string{CoverSpecificRoomLocation, CoverIdentifier})
	if assessment.Answerable || assessment.DecisionReason != "required_cover_missing" {
		t.Fatalf("assessment=%#v, want missing-cover rejection", assessment)
	}
}

func TestRetrieveAnswerableCanBecomeAnswerableWithNewCoveredKnowledge(t *testing.T) {
	items := []KnowledgeItem{
		testKnowledgeWithCovers("phone", "园区联系电话是多少", "园区联系电话为内部登记号码。", []string{CoverContactPhone}, []float64{1}),
	}
	store, err := NewStore(context.Background(), items, fixedEmbedder{vec: []float64{1}})
	if err != nil {
		t.Fatalf("NewStore: %v", err)
	}
	retriever := NewRetriever(store, fixedEmbedder{vec: []float64{1}}, Config{TopK: 3, MinScore: 0.5})
	assessment, contextText, err := retriever.RetrieveAnswerable(context.Background(), "园区联系电话是多少")
	if err != nil {
		t.Fatalf("RetrieveAnswerable: %v", err)
	}
	if !assessment.Answerable || !strings.Contains(contextText, "园区联系电话") {
		t.Fatalf("assessment=%#v context=%q, want answerable contact phone", assessment, contextText)
	}
}

func TestCoversDoNotEnterEmbeddingOrPrompt(t *testing.T) {
	item := KnowledgeItem{Category: "cat", Question: "Q", Answer: "A", Keywords: []string{"k"}, Covers: []string{CoverContactPhone}}
	if strings.Contains(item.TextForEmbedding(), CoverContactPhone) {
		t.Fatalf("TextForEmbedding leaked covers: %q", item.TextForEmbedding())
	}
	ctx := BuildContext([]Result{{Item: item, Score: 1}}, DefaultConfig())
	request := BuildLLMRequest(ctx, "园区联系电话是多少")
	if strings.Contains(request.UserData, CoverContactPhone) || strings.Contains(request.SystemPrompt, CoverContactPhone) {
		t.Fatalf("prompt leaked covers: %#v", request)
	}
}

func TestRetrieveAnswerableFailClosedBeforeEmbedding(t *testing.T) {
	items := []KnowledgeItem{testKnowledgeWithCovers("a", "Q", "A", []string{CoverFacilityLocation}, []float64{1})}
	embedder := &countingEmbedder{vec: []float64{1}}
	store, err := NewStore(context.Background(), items, embedder)
	if err != nil {
		t.Fatalf("NewStore: %v", err)
	}
	callsAfterIndex := embedder.calls
	retriever := NewRetriever(store, embedder, Config{TopK: 3, MinScore: 0.5})
	assessment, _, err := retriever.RetrieveAnswerable(context.Background(), "园区这个情况应该怎么判断")
	if err != nil {
		t.Fatalf("RetrieveAnswerable: %v", err)
	}
	if assessment.Answerable || assessment.DecisionReason != "required_covers_unrecognized" {
		t.Fatalf("assessment=%#v, want fail-closed", assessment)
	}
	if embedder.calls != callsAfterIndex {
		t.Fatalf("embedder calls=%d, want unchanged %d for unrecognized required_covers", embedder.calls, callsAfterIndex)
	}
}

type countingEmbedder struct {
	vec   []float64
	calls int
}

func (e *countingEmbedder) Embed(context.Context, string) ([]float64, error) {
	e.calls++
	return append([]float64(nil), e.vec...), nil
}

func testKnowledgeWithCovers(id, question, answer string, covers []string, embedding []float64) KnowledgeItem {
	item := testKnowledge(id, question, answer, embedding)
	item.Covers = covers
	return item
}

func TestEvaluateAnswerabilityKeepsAllIntersectingCandidates(t *testing.T) {
	results := []Result{
		{Item: KnowledgeItem{ID: "wrong_top1", Covers: []string{CoverSafetyGuidance}}, Score: 0.92},
		{Item: KnowledgeItem{ID: "correct_top2", Covers: []string{CoverSafetyGuidance}}, Score: 0.91},
		{Item: KnowledgeItem{ID: "unrelated", Covers: []string{CoverFacilityLocation}}, Score: 0.9},
	}
	assessment := EvaluateAnswerability(results, []string{CoverSafetyGuidance})
	if !assessment.Answerable {
		t.Fatalf("assessment answerable=false: %#v", assessment)
	}
	if got, want := resultIDs(assessment.RawResults), []string{"wrong_top1", "correct_top2", "unrelated"}; !reflect.DeepEqual(got, want) {
		t.Fatalf("raw ids=%v, want %v", got, want)
	}
	if got, want := assessment.EligibleHitIDs, []string{"wrong_top1", "correct_top2"}; !reflect.DeepEqual(got, want) {
		t.Fatalf("eligible ids=%v, want %v", got, want)
	}
}

func TestEvaluateAnswerabilityAfterThresholdRequiresAllCovers(t *testing.T) {
	raw := []Result{
		{Item: KnowledgeItem{ID: "room", Covers: []string{CoverSpecificRoomLocation}}, Score: 0.8},
		{Item: KnowledgeItem{ID: "identifier", Covers: []string{CoverIdentifier}}, Score: 0.49},
	}
	filtered := filterResultsByThreshold(raw, 0.5)
	assessment := EvaluateAnswerability(filtered, []string{CoverSpecificRoomLocation, CoverIdentifier})
	if assessment.Answerable {
		t.Fatalf("assessment=%#v, want reject when identifier is filtered out", assessment)
	}
	if got, want := resultIDs(assessment.RawResults), []string{"room"}; !reflect.DeepEqual(got, want) {
		t.Fatalf("raw ids after threshold=%v, want %v", got, want)
	}
}

func filterResultsByThreshold(results []Result, threshold float64) []Result {
	var out []Result
	for _, result := range results {
		if result.Score >= threshold {
			out = append(out, result)
		}
	}
	return out
}
