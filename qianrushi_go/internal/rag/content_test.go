package rag

import (
	"encoding/json"
	"os"
	"path/filepath"
	"strings"
	"testing"
)

type ragEvalCase struct {
	CaseID        string `json:"case_id"`
	Query         string `json:"query"`
	ExpectedID    string `json:"expected_id"`
	ExpectedRoute string `json:"expected_route"`
	CoreSafety    bool   `json:"core_safety"`
	Notes         string `json:"notes"`
}

func TestStage004CActiveKnowledgeContent(t *testing.T) {
	items, err := LoadDir(filepath.Join("..", "..", "knowledge"))
	if err != nil {
		t.Fatalf("LoadDir active knowledge: %v", err)
	}
	if got, want := len(items), 30; got != want {
		t.Fatalf("active knowledge count=%d, want %d", got, want)
	}

	wantCategories := map[string]int{
		"园区位置与设施": 6,
		"访客与便民服务": 5,
		"安全规范":    6,
		"应急流程":    5,
		"设备操作与故障": 6,
		"常见问题":    2,
	}
	gotCategories := make(map[string]int)
	seenIDs := make(map[string]bool)
	bannedUserVisible := []string{"演示园区", "模拟园区", "演示数据", "假设位置"}
	for _, item := range items {
		if seenIDs[item.ID] {
			t.Fatalf("duplicate id %q", item.ID)
		}
		seenIDs[item.ID] = true
		gotCategories[item.Category]++
		if len(item.Embedding) != 0 {
			t.Fatalf("active knowledge %q contains embedding", item.ID)
		}
		if !item.Enabled {
			t.Fatalf("active knowledge %q enabled=false, want true for 004C-1 active set", item.ID)
		}
		if err := item.Validate(); err != nil {
			t.Fatalf("active knowledge %q invalid: %v", item.ID, err)
		}
		if len(item.Covers) == 0 {
			t.Fatalf("active knowledge %q covers empty", item.ID)
		}
		visibleText := item.Question + "\n" + item.Answer + "\n" + strings.Join(item.Keywords, "\n")
		for _, banned := range bannedUserVisible {
			if strings.Contains(visibleText, banned) {
				t.Fatalf("active knowledge %q contains banned user-visible phrase %q", item.ID, banned)
			}
		}
	}
	assertContainsAll(t, itemByID(items, "park_001").Answer, []string{"主入口东侧", "一层服务大厅东侧"})
	assertContainsAll(t, itemByID(items, "loc_restroom").Answer, []string{"一层服务大厅西侧"})
	assertContainsAll(t, itemByID(items, "loc_rest_area").Answer, []string{"一层服务大厅北侧"})
	assertContainsAll(t, itemByID(items, "loc_entrance_exit").Answer, []string{"园区南侧", "一层服务大厅"})
	assertContainsAll(t, itemByID(items, "loc_parking_shuttle").Answer, []string{"园区南侧", "停车区北侧"})
	assertContainsAll(t, itemByID(items, "loc_accessibility").Answer, []string{"主入口西侧", "一层服务大厅西侧"})
	assertContainsAll(t, itemByID(items, "visitor_registration").Answer, []string{"南侧主入口", "主入口东侧访客中心"})
	assertContainsAll(t, itemByID(items, "visitor_consultation").Answer, []string{"主入口东侧"})
	for category, want := range wantCategories {
		if got := gotCategories[category]; got != want {
			t.Fatalf("category %q count=%d, want %d; all=%v", category, got, want, gotCategories)
		}
	}
	if len(gotCategories) != len(wantCategories) {
		t.Fatalf("unexpected categories: %v", gotCategories)
	}
}

func TestStage004CRAGEvalCases(t *testing.T) {
	items, err := LoadDir(filepath.Join("..", "..", "knowledge"))
	if err != nil {
		t.Fatalf("LoadDir active knowledge: %v", err)
	}
	ids := make(map[string]bool, len(items))
	for _, item := range items {
		ids[item.ID] = true
	}

	data, err := os.ReadFile(filepath.Join("..", "..", "eval", "rag_cases.json"))
	if err != nil {
		t.Fatalf("read eval cases: %v", err)
	}
	var cases []ragEvalCase
	if err := json.Unmarshal(data, &cases); err != nil {
		t.Fatalf("decode eval cases: %v", err)
	}

	seenCaseIDs := make(map[string]bool, len(cases))
	counts := map[string]int{"rag_hit": 0, "rag_no_data": 0, "chat": 0, "map": 0, "local_env": 0}
	hitsByID := make(map[string]int)
	coreSafetyIDs := make(map[string]bool)
	bannedEvalQuery := []string{"演示园区", "模拟园区", "演示数据", "假设位置"}
	for _, tc := range cases {
		for _, banned := range bannedEvalQuery {
			if strings.Contains(tc.Query, banned) {
				t.Fatalf("case %s query contains banned phrase %q", tc.CaseID, banned)
			}
		}
		if tc.CaseID == "" || tc.Query == "" {
			t.Fatalf("case has empty id/query: %#v", tc)
		}
		if seenCaseIDs[tc.CaseID] {
			t.Fatalf("duplicate case_id %q", tc.CaseID)
		}
		seenCaseIDs[tc.CaseID] = true
		if _, ok := counts[tc.ExpectedRoute]; !ok {
			t.Fatalf("case %s has invalid expected_route %q", tc.CaseID, tc.ExpectedRoute)
		}
		counts[tc.ExpectedRoute]++
		switch tc.ExpectedRoute {
		case "rag_hit":
			if !ids[tc.ExpectedID] {
				t.Fatalf("case %s expected_id %q not found in active knowledge", tc.CaseID, tc.ExpectedID)
			}
			hitsByID[tc.ExpectedID]++
		case "rag_no_data", "chat", "map", "local_env":
			if tc.ExpectedID != "" {
				t.Fatalf("case %s route=%s expected_id=%q, want empty", tc.CaseID, tc.ExpectedRoute, tc.ExpectedID)
			}
		}
		if tc.ExpectedRoute == "rag_hit" || tc.ExpectedRoute == "rag_no_data" {
			covers, ok := RequiredCovers(tc.Query)
			if !ok || len(covers) == 0 {
				t.Fatalf("case %s route=%s query=%q required covers=(%v,%v), want non-empty", tc.CaseID, tc.ExpectedRoute, tc.Query, covers, ok)
			}
		}
		if tc.CoreSafety {
			if tc.ExpectedRoute != "rag_hit" {
				t.Fatalf("case %s core_safety route=%s, want rag_hit", tc.CaseID, tc.ExpectedRoute)
			}
			coreSafetyIDs[tc.ExpectedID] = true
		}
	}
	if counts["rag_hit"] < 60 || counts["rag_no_data"] < 15 || counts["chat"] < 10 {
		t.Fatalf("eval counts=%v, want at least rag_hit=60 rag_no_data=15 chat=10", counts)
	}
	if counts["map"] < 2 {
		t.Fatalf("eval map count=%d, want at least 2", counts["map"])
	}
	if counts["local_env"] < 2 {
		t.Fatalf("eval local_env count=%d, want at least 2", counts["local_env"])
	}
	for id := range ids {
		if hitsByID[id] < 2 {
			t.Fatalf("knowledge %q has %d hit cases, want at least 2", id, hitsByID[id])
		}
	}
	for _, id := range []string{"emergency_fire", "safety_002", "emergency_injury", "safety_electricity", "emergency_equipment_abnormal"} {
		if !coreSafetyIDs[id] {
			t.Fatalf("core_safety missing required knowledge id %q; got %v", id, coreSafetyIDs)
		}
	}
}

func itemByID(items []KnowledgeItem, id string) KnowledgeItem {
	for _, item := range items {
		if item.ID == id {
			return item
		}
	}
	return KnowledgeItem{}
}

func assertContainsAll(t *testing.T, text string, parts []string) {
	t.Helper()
	for _, part := range parts {
		if !strings.Contains(text, part) {
			t.Fatalf("%q does not contain required layout phrase %q", text, part)
		}
	}
}
