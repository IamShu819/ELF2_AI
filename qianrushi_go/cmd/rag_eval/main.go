package main

import (
	"context"
	"encoding/json"
	"flag"
	"fmt"
	"math"
	"os"
	"path/filepath"
	"sort"
	"strings"
	"time"

	"comm-gateway/internal/rag"
	voicews "comm-gateway/internal/websocket"
)

const (
	evalModeScan              = "scan"
	evalModeProductionDefault = "production-default"
)

type evalCase struct {
	CaseID        string `json:"case_id"`
	Query         string `json:"query"`
	ExpectedID    string `json:"expected_id"`
	ExpectedRoute string `json:"expected_route"`
	CoreSafety    bool   `json:"core_safety"`
	Notes         string `json:"notes"`
}

type evalResult struct {
	CaseID         string       `json:"case_id"`
	Query          string       `json:"query"`
	ExpectedID     string       `json:"expected_id"`
	ExpectedRoute  string       `json:"expected_route"`
	ActualRoute    string       `json:"actual_route"`
	CoreSafety     bool         `json:"core_safety"`
	TopK           int          `json:"top_k"`
	MinScore       float64      `json:"min_score"`
	HitIDs         []string     `json:"hit_ids"`
	Scores         []float64    `json:"scores"`
	Selected       string       `json:"selected"`
	RequiredCovers []string     `json:"required_covers"`
	MatchedCovers  []string     `json:"matched_covers"`
	EligibleHitIDs []string     `json:"eligible_hit_ids"`
	Answerable     bool         `json:"answerable"`
	DecisionReason string       `json:"decision_reason"`
	FinalAnswer    string       `json:"final_answer"`
	Notes          string       `json:"notes"`
	RawResults     []rag.Result `json:"-"`
}

type thresholdRow struct {
	Threshold              float64 `json:"threshold"`
	RAGHitRetentionRate    float64 `json:"rag_hit_retention_rate"`
	Top1Accuracy           float64 `json:"top1_accuracy"`
	Top3CoverageRate       float64 `json:"top3_coverage_rate"`
	NoDataFalseHitRate     float64 `json:"no_data_false_hit_rate"`
	CoreSafetyTop1Accuracy float64 `json:"core_safety_top1_accuracy"`
}

type summary struct {
	GeneratedAt            string             `json:"generated_at"`
	RouteAccuracy          float64            `json:"route_accuracy"`
	RouteCorrect           int                `json:"route_correct"`
	RouteTotal             int                `json:"route_total"`
	RAGHitTop1Accuracy     float64            `json:"rag_hit_top1_accuracy"`
	RAGHitTop3CoverageRate float64            `json:"rag_hit_top3_coverage_rate"`
	CoreSafetyTop1Accuracy float64            `json:"core_safety_top1_accuracy"`
	NoDataFalseHitRate     float64            `json:"no_data_false_hit_rate"`
	NoDataTop1Scores       []float64          `json:"no_data_top1_scores"`
	CategoryAccuracy       map[string]float64 `json:"category_accuracy"`
	ThresholdCandidates    []thresholdRow     `json:"threshold_candidates"`
	SelectedThreshold      float64            `json:"selected_threshold"`
	HasSelectedThreshold   bool               `json:"has_selected_threshold"`
}

type output struct {
	Config  map[string]interface{} `json:"config"`
	Summary summary                `json:"summary"`
	Results []evalResult           `json:"results"`
}

func main() {
	casesPath := flag.String("cases", "eval/rag_cases.json", "eval cases JSON")
	knowledgeDir := flag.String("knowledge", "knowledge", "knowledge directory")
	mode := flag.String("mode", evalModeProductionDefault, "evaluation mode: scan or production-default")
	outJSON := flag.String("out-json", "", "output JSON path; defaults depend on mode")
	outSummary := flag.String("out-summary", "", "output markdown summary path; defaults depend on mode")
	embeddingURL := flag.String("embedding-url", "http://127.0.0.1:8001/v1/embeddings", "embedding endpoint")
	embeddingModel := flag.String("embedding-model", rag.DefaultRemoteEmbeddingModel, "embedding model")
	timeoutMS := flag.Int("timeout-ms", rag.DefaultRemoteEmbeddingTimeoutMS, "embedding timeout in ms")
	flag.Parse()

	if err := run(*mode, *casesPath, *knowledgeDir, *outJSON, *outSummary, *embeddingURL, *embeddingModel, *timeoutMS); err != nil {
		fmt.Fprintf(os.Stderr, "rag eval failed: %v\n", err)
		os.Exit(1)
	}
}

func run(mode, casesPath, knowledgeDir, outJSON, outSummary, embeddingURL, embeddingModel string, timeoutMS int) error {
	mode = strings.TrimSpace(mode)
	retrievalConfig, err := retrievalConfigForMode(mode)
	if err != nil {
		return err
	}
	if outJSON == "" || outSummary == "" {
		defaultJSON, defaultSummary := defaultOutputPaths(mode)
		if outJSON == "" {
			outJSON = defaultJSON
		}
		if outSummary == "" {
			outSummary = defaultSummary
		}
	}
	cases, err := loadCases(casesPath)
	if err != nil {
		return err
	}
	items, err := rag.LoadDir(knowledgeDir)
	if err != nil {
		return fmt.Errorf("load knowledge: %w", err)
	}
	categoryByID := make(map[string]string, len(items))
	for _, item := range items {
		categoryByID[item.ID] = item.Category
	}

	ctx := context.Background()
	rt := rag.NewRuntime(ctx, rag.RuntimeConfig{
		Enabled:            true,
		KnowledgeDir:       knowledgeDir,
		EmbeddingBackend:   "remote-http",
		EmbeddingURL:       embeddingURL,
		EmbeddingModel:     embeddingModel,
		EmbeddingTimeoutMS: timeoutMS,
		Retrieval:          retrievalConfig,
	})
	if !rt.Enabled() {
		return fmt.Errorf("RAG runtime unavailable: %w", rt.LoadError())
	}

	results := make([]evalResult, 0, len(cases))
	for _, tc := range cases {
		actualRoute := voicews.ProductionRoute(tc.Query)
		res := evalResult{
			CaseID:        tc.CaseID,
			Query:         tc.Query,
			ExpectedID:    tc.ExpectedID,
			ExpectedRoute: tc.ExpectedRoute,
			ActualRoute:   actualRoute,
			CoreSafety:    tc.CoreSafety,
			TopK:          retrievalConfig.TopK,
			MinScore:      retrievalConfig.MinScore,
			FinalAnswer:   "",
			Notes:         strings.TrimSpace(tc.Notes + "; 本轮未调用 LLM"),
		}
		if actualRoute == voicews.ProductionRouteRAG {
			assessment, _, _, err := rt.RetrieveAnswerable(ctx, tc.Query)
			if err != nil {
				return fmt.Errorf("retrieve %s: %w", tc.CaseID, err)
			}
			res.RequiredCovers = assessment.RequiredCovers
			res.MatchedCovers = assessment.MatchedCovers
			res.EligibleHitIDs = assessment.EligibleHitIDs
			res.Answerable = assessment.Answerable
			res.DecisionReason = assessment.DecisionReason
			res.RawResults = assessment.RawResults
			for _, hit := range assessment.RawResults {
				res.HitIDs = append(res.HitIDs, hit.Item.ID)
				res.Scores = append(res.Scores, round6(hit.Score))
			}
			if len(res.EligibleHitIDs) > 0 {
				res.Selected = res.EligibleHitIDs[0]
			}
		}
		results = append(results, res)
	}

	sum := summarize(cases, results, categoryByID)
	out := output{
		Config: map[string]interface{}{
			"mode":                     mode,
			"formal_default_min_score": rag.DefaultConfig().MinScore,
			"embedding_model":          embeddingModel,
			"embedding_url":            embeddingURL,
			"embedding_dimension":      rag.RemoteEmbeddingDimension,
			"top_k":                    retrievalConfig.TopK,
			"min_score":                retrievalConfig.MinScore,
			"final_answer":             "not evaluated; LLM not called",
		},
		Summary: sum,
		Results: results,
	}
	if err := writeJSON(outJSON, out); err != nil {
		return err
	}
	if err := writeMarkdown(outSummary, out, categoryByID); err != nil {
		return err
	}
	return nil
}

func retrievalConfigForMode(mode string) (rag.Config, error) {
	switch mode {
	case evalModeScan:
		return rag.Config{TopK: rag.DefaultTopK, MinScore: -1, MinScoreSet: true, MaxItemRunes: rag.DefaultMaxItemRunes, MaxContextRunes: rag.DefaultMaxContextRunes}, nil
	case evalModeProductionDefault:
		return rag.DefaultConfig(), nil
	default:
		return rag.Config{}, fmt.Errorf("unsupported mode %q; want %q or %q", mode, evalModeScan, evalModeProductionDefault)
	}
}

func defaultOutputPaths(mode string) (string, string) {
	if mode == evalModeScan {
		return "eval/results/004c3-scan.json", "eval/results/004c3-scan-summary.md"
	}
	return "eval/results/004c3-final.json", "eval/results/004c3-summary.md"
}

func loadCases(path string) ([]evalCase, error) {
	data, err := os.ReadFile(path)
	if err != nil {
		return nil, err
	}
	var cases []evalCase
	if err := json.Unmarshal(data, &cases); err != nil {
		return nil, err
	}
	return cases, nil
}

func expectedProductionRoute(route string) string {
	switch route {
	case "rag_hit", "rag_no_data":
		return voicews.ProductionRouteRAG
	case "local_env":
		return voicews.ProductionRouteLocalEnv
	default:
		return route
	}
}

func summarize(cases []evalCase, results []evalResult, categoryByID map[string]string) summary {
	var routeCorrect, hitTotal, hitTop1, hitTop3, coreTotal, coreTop1, noDataTotal, noDataFalse int
	catTotal := map[string]int{}
	catCorrect := map[string]int{}
	var noDataScores []float64
	for i, tc := range cases {
		res := results[i]
		if res.ActualRoute == expectedProductionRoute(tc.ExpectedRoute) {
			routeCorrect++
		}
		if tc.ExpectedRoute == "rag_hit" {
			hitTotal++
			cat := categoryByID[tc.ExpectedID]
			catTotal[cat]++
			if len(res.EligibleHitIDs) > 0 && res.EligibleHitIDs[0] == tc.ExpectedID {
				hitTop1++
				catCorrect[cat]++
			}
			if containsID(res.EligibleHitIDs, tc.ExpectedID) {
				hitTop3++
			}
			if tc.CoreSafety {
				coreTotal++
				if len(res.EligibleHitIDs) > 0 && res.EligibleHitIDs[0] == tc.ExpectedID {
					coreTop1++
				}
			}
		}
		if tc.ExpectedRoute == "rag_no_data" {
			noDataTotal++
			if res.Answerable {
				noDataFalse++
			}
			if len(res.Scores) > 0 {
				noDataScores = append(noDataScores, res.Scores[0])
			}
		}
	}
	sort.Float64s(noDataScores)
	catAcc := make(map[string]float64, len(catTotal))
	for cat, total := range catTotal {
		catAcc[cat] = rate(catCorrect[cat], total)
	}
	rows := thresholdRows(cases, results)
	selected, ok := selectedThreshold(rows)
	return summary{
		GeneratedAt:            time.Now().Format(time.RFC3339),
		RouteAccuracy:          rate(routeCorrect, len(cases)),
		RouteCorrect:           routeCorrect,
		RouteTotal:             len(cases),
		RAGHitTop1Accuracy:     rate(hitTop1, hitTotal),
		RAGHitTop3CoverageRate: rate(hitTop3, hitTotal),
		CoreSafetyTop1Accuracy: rate(coreTop1, coreTotal),
		NoDataFalseHitRate:     rate(noDataFalse, noDataTotal),
		NoDataTop1Scores:       noDataScores,
		CategoryAccuracy:       catAcc,
		ThresholdCandidates:    rows,
		SelectedThreshold:      selected,
		HasSelectedThreshold:   ok,
	}
}

func thresholdRows(cases []evalCase, results []evalResult) []thresholdRow {
	minScore, maxScore := math.Inf(1), math.Inf(-1)
	for _, res := range results {
		for _, score := range res.Scores {
			if score < minScore {
				minScore = score
			}
			if score > maxScore {
				maxScore = score
			}
		}
	}
	if math.IsInf(minScore, 0) || math.IsInf(maxScore, 0) {
		return nil
	}
	start := math.Floor(minScore*100) / 100
	end := math.Ceil(maxScore*100) / 100
	var rows []thresholdRow
	for th := start; th <= end+1e-9; th += 0.01 {
		threshold := round2(th)
		var hitTotal, retained, top1, top3, noDataTotal, noDataFalse, coreTotal, coreTop1 int
		for i, tc := range cases {
			res := results[i]
			switch tc.ExpectedRoute {
			case "rag_hit":
				hitTotal++
				assessment := rag.EvaluateAnswerability(resultsAtThreshold(res.RawResults, threshold), res.RequiredCovers)
				keptIDs := assessment.EligibleHitIDs
				if assessment.Answerable && len(keptIDs) > 0 {
					retained++
				}
				if len(keptIDs) > 0 && keptIDs[0] == tc.ExpectedID {
					top1++
				}
				if containsID(keptIDs, tc.ExpectedID) {
					top3++
				}
				if tc.CoreSafety {
					coreTotal++
					if len(keptIDs) > 0 && keptIDs[0] == tc.ExpectedID {
						coreTop1++
					}
				}
			case "rag_no_data":
				noDataTotal++
				assessment := rag.EvaluateAnswerability(resultsAtThreshold(res.RawResults, threshold), res.RequiredCovers)
				if assessment.Answerable {
					noDataFalse++
				}
			}
		}
		rows = append(rows, thresholdRow{threshold, rate(retained, hitTotal), rate(top1, hitTotal), rate(top3, hitTotal), rate(noDataFalse, noDataTotal), rate(coreTop1, coreTotal)})
	}
	return rows
}

func selectedThreshold(rows []thresholdRow) (float64, bool) {
	var selected float64
	ok := false
	for _, row := range rows {
		if row.Top1Accuracy >= 0.90 && row.Top3CoverageRate >= 0.95 && row.NoDataFalseHitRate <= 0.10 && row.CoreSafetyTop1Accuracy >= 1.0 {
			if !ok || row.Threshold > selected {
				selected = row.Threshold
				ok = true
			}
		}
	}
	return selected, ok
}

func resultsAtThreshold(results []rag.Result, threshold float64) []rag.Result {
	var kept []rag.Result
	for _, result := range results {
		if result.Score >= threshold {
			kept = append(kept, result)
		}
	}
	return kept
}

func writeJSON(path string, out output) error {
	if err := os.MkdirAll(filepath.Dir(path), 0o755); err != nil {
		return err
	}
	data, err := json.MarshalIndent(out, "", "  ")
	if err != nil {
		return err
	}
	return os.WriteFile(path, append(data, '\n'), 0o644)
}

func writeMarkdown(path string, out output, categoryByID map[string]string) error {
	if err := os.MkdirAll(filepath.Dir(path), 0o755); err != nil {
		return err
	}
	var b strings.Builder
	s := out.Summary
	fmt.Fprintf(&b, "# 004C-3 RAG answerability threshold summary\n\n")
	fmt.Fprintf(&b, "- Generated at: %s\n", s.GeneratedAt)
	fmt.Fprintf(&b, "- Model: %s\n", out.Config["embedding_model"])
	fmt.Fprintf(&b, "- Mode: %v\n", out.Config["mode"])
	fmt.Fprintf(&b, "- Top K: %v\n", out.Config["top_k"])
	fmt.Fprintf(&b, "- Min score: %v\n", out.Config["min_score"])
	fmt.Fprintf(&b, "- Formal default min score: %v\n", out.Config["formal_default_min_score"])
	fmt.Fprintf(&b, "- LLM/TTS/ASR: not called; final_answer is empty by design.\n\n")
	fmt.Fprintf(&b, "## Metrics\n\n")
	fmt.Fprintf(&b, "- Route accuracy: %.2f%% (%d/%d)\n", s.RouteAccuracy*100, s.RouteCorrect, s.RouteTotal)
	fmt.Fprintf(&b, "- rag_hit top-1 accuracy: %.2f%%\n", s.RAGHitTop1Accuracy*100)
	fmt.Fprintf(&b, "- rag_hit expected id in top-3: %.2f%%\n", s.RAGHitTop3CoverageRate*100)
	fmt.Fprintf(&b, "- core_safety top-1 accuracy: %.2f%%\n", s.CoreSafetyTop1Accuracy*100)
	fmt.Fprintf(&b, "- no-data final false-hit rate: %.2f%%\n", s.NoDataFalseHitRate*100)
	fmt.Fprintf(&b, "- no-data top-1 score distribution: %v\n", s.NoDataTop1Scores)
	if s.HasSelectedThreshold {
		fmt.Fprintf(&b, "- Threshold conclusion: 最高达标候选阈值为 %.2f。\n", s.SelectedThreshold)
	} else {
		fmt.Fprintf(&b, "- Threshold conclusion: 当前不存在同时满足全部 Spec 指标的全局阈值；不得修改正式 VOICE_RAG_MIN_SCORE。\n")
	}
	fmt.Fprintf(&b, "\n")
	fmt.Fprintf(&b, "## Category top-1 accuracy\n\n")
	cats := make([]string, 0, len(s.CategoryAccuracy))
	for cat := range s.CategoryAccuracy {
		cats = append(cats, cat)
	}
	sort.Strings(cats)
	for _, cat := range cats {
		fmt.Fprintf(&b, "- %s: %.2f%%\n", cat, s.CategoryAccuracy[cat]*100)
	}
	fmt.Fprintf(&b, "\n## Error samples\n\n")
	for _, res := range out.Results {
		if res.ExpectedRoute == "rag_hit" && (len(res.EligibleHitIDs) == 0 || res.EligibleHitIDs[0] != res.ExpectedID) {
			fmt.Fprintf(&b, "- %s expected=%s eligible=%v raw_top3=%v scores=%v query=%q\n", res.CaseID, res.ExpectedID, res.EligibleHitIDs, res.HitIDs, res.Scores, res.Query)
		}
	}
	fmt.Fprintf(&b, "\n## Threshold candidates\n\n")
	fmt.Fprintf(&b, "| threshold | hit retention | top-1 | top-3 | no-data false hit | core top-1 |\n")
	fmt.Fprintf(&b, "| ---: | ---: | ---: | ---: | ---: | ---: |\n")
	for _, row := range s.ThresholdCandidates {
		fmt.Fprintf(&b, "| %.2f | %.2f%% | %.2f%% | %.2f%% | %.2f%% | %.2f%% |\n", row.Threshold, row.RAGHitRetentionRate*100, row.Top1Accuracy*100, row.Top3CoverageRate*100, row.NoDataFalseHitRate*100, row.CoreSafetyTop1Accuracy*100)
	}
	return os.WriteFile(path, []byte(b.String()), 0o644)
}

func containsID(ids []string, id string) bool {
	for _, got := range ids {
		if got == id {
			return true
		}
	}
	return false
}

func rate(n, d int) float64 {
	if d == 0 {
		return 0
	}
	return float64(n) / float64(d)
}

func round6(v float64) float64 { return math.Round(v*1e6) / 1e6 }
func round2(v float64) float64 { return math.Round(v*100) / 100 }
