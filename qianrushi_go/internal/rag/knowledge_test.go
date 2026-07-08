package rag

import (
	"os"
	"path/filepath"
	"strings"
	"testing"
)

func TestLoadDirLoadsKnowledgeAndSortsFiles(t *testing.T) {
	dir := t.TempDir()
	writeFile(t, dir, "b.json", `{"id":"b","category":"cat","question":"B","answer":"answer b","keywords":["b"],"source":"demo","enabled":true,"covers":["facility_location"],"embedding":[0,1]}`)
	writeFile(t, dir, "a.json", `[{"id":"a","category":"cat","question":"A","answer":"answer a","keywords":["a"],"source":"demo","enabled":true,"covers":["facility_location"],"embedding":[1,0]}]`)

	items, err := LoadDir(dir)
	if err != nil {
		t.Fatalf("LoadDir returned error: %v", err)
	}
	if got, want := len(items), 2; got != want {
		t.Fatalf("len(items)=%d, want %d", got, want)
	}
	if items[0].ID != "a" || items[1].ID != "b" {
		t.Fatalf("items loaded in unexpected order: %#v", []string{items[0].ID, items[1].ID})
	}
}

func TestLoadFileValidatesRequiredFields(t *testing.T) {
	dir := t.TempDir()
	path := filepath.Join(dir, "bad.json")
	if err := os.WriteFile(path, []byte(`{"id":"bad","category":"cat","question":"missing answer","keywords":["bad"],"source":"demo","enabled":true}`), 0o644); err != nil {
		t.Fatal(err)
	}

	_, err := LoadFile(path)
	if err == nil {
		t.Fatal("LoadFile succeeded, want validation error")
	}
	if !strings.Contains(err.Error(), "answer is required") {
		t.Fatalf("error=%q, want answer validation", err.Error())
	}
}

func TestLoadDirRejectsDuplicateIDs(t *testing.T) {
	dir := t.TempDir()
	writeFile(t, dir, "a.json", `{"id":"dup","category":"cat","question":"A","answer":"answer a","keywords":["a"],"source":"demo","enabled":true,"covers":["facility_location"],"embedding":[1,0]}`)
	writeFile(t, dir, "b.json", `{"id":"dup","category":"cat","question":"B","answer":"answer b","keywords":["b"],"source":"demo","enabled":true,"covers":["facility_location"],"embedding":[0,1]}`)

	_, err := LoadDir(dir)
	if err == nil {
		t.Fatal("LoadDir succeeded, want duplicate id error")
	}
	if !strings.Contains(err.Error(), "duplicate knowledge id") {
		t.Fatalf("error=%q, want duplicate id validation", err.Error())
	}
}

func writeFile(t *testing.T, dir, name, content string) {
	t.Helper()
	if err := os.WriteFile(filepath.Join(dir, name), []byte(content), 0o644); err != nil {
		t.Fatal(err)
	}
}

func TestLoadFileValidatesStage004Fields(t *testing.T) {
	cases := []struct {
		name    string
		content string
		want    string
	}{
		{"missing category", `{"id":"bad","question":"Q","answer":"A","keywords":["k"],"source":"demo","enabled":true}`, "category is required"},
		{"missing keywords", `{"id":"bad","category":"cat","question":"Q","answer":"A","source":"demo","enabled":true}`, "keywords are required"},
		{"missing enabled", `{"id":"bad","category":"cat","question":"Q","answer":"A","keywords":["k"],"source":"demo"}`, "enabled is required"},
		{"bad source", `{"id":"bad","category":"cat","question":"Q","answer":"A","keywords":["k"],"source":"made-up","enabled":true,"covers":["facility_location"]}`, "source"},
	}
	for _, tc := range cases {
		t.Run(tc.name, func(t *testing.T) {
			dir := t.TempDir()
			path := filepath.Join(dir, "bad.json")
			if err := os.WriteFile(path, []byte(tc.content), 0o644); err != nil {
				t.Fatal(err)
			}
			_, err := LoadFile(path)
			if err == nil || !strings.Contains(err.Error(), tc.want) {
				t.Fatalf("err=%v, want %q", err, tc.want)
			}
		})
	}
}

func TestLoadFileValidatesCovers(t *testing.T) {
	cases := []struct {
		name    string
		content string
		want    string
	}{
		{"missing covers enabled", `{"id":"bad","category":"cat","question":"Q","answer":"A","keywords":["k"],"source":"demo","enabled":true}`, "covers are required"},
		{"empty cover", `{"id":"bad","category":"cat","question":"Q","answer":"A","keywords":["k"],"source":"demo","enabled":true,"covers":[""]}`, "empty value"},
		{"unknown cover", `{"id":"bad","category":"cat","question":"Q","answer":"A","keywords":["k"],"source":"demo","enabled":true,"covers":["unknown"]}`, "invalid"},
		{"duplicate cover", `{"id":"bad","category":"cat","question":"Q","answer":"A","keywords":["k"],"source":"demo","enabled":true,"covers":["facility_location","facility_location"]}`, "duplicated"},
	}
	for _, tc := range cases {
		t.Run(tc.name, func(t *testing.T) {
			dir := t.TempDir()
			path := filepath.Join(dir, "bad.json")
			content := strings.ReplaceAll(tc.content, `\"`, `"`)
			if err := os.WriteFile(path, []byte(content), 0o644); err != nil {
				t.Fatal(err)
			}
			_, err := LoadFile(path)
			if err == nil || !strings.Contains(err.Error(), tc.want) {
				t.Fatalf("err=%v, want %q", err, tc.want)
			}
		})
	}
}

func TestEnabledKnowledgeFiltersDisabled(t *testing.T) {
	items := []KnowledgeItem{
		{ID: "on", Enabled: true},
		{ID: "off", Enabled: false},
	}
	got := enabledKnowledge(items)
	if len(got) != 1 || got[0].ID != "on" {
		t.Fatalf("enabledKnowledge=%#v", got)
	}
}
