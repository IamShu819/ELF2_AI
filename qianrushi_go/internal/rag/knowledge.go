package rag

import (
	"encoding/json"
	"errors"
	"fmt"
	"os"
	"path/filepath"
	"sort"
	"strings"
)

const (
	SourceDemo          = "demo"
	SourceUserConfirmed = "user-confirmed"
	SourcePublicGeneral = "public-general"
)

type KnowledgeItem struct {
	ID         string    `json:"id"`
	Category   string    `json:"category"`
	Question   string    `json:"question"`
	Answer     string    `json:"answer"`
	Keywords   []string  `json:"keywords"`
	Covers     []string  `json:"covers"`
	Source     string    `json:"source"`
	Enabled    bool      `json:"enabled"`
	EnabledSet bool      `json:"-"`
	Embedding  []float64 `json:"embedding,omitempty"`
}

type knowledgeItemJSON struct {
	ID        string    `json:"id"`
	Category  string    `json:"category"`
	Question  string    `json:"question"`
	Answer    string    `json:"answer"`
	Keywords  []string  `json:"keywords"`
	Covers    []string  `json:"covers"`
	Source    string    `json:"source"`
	Enabled   *bool     `json:"enabled"`
	Embedding []float64 `json:"embedding"`
}

func (i *KnowledgeItem) UnmarshalJSON(data []byte) error {
	var raw knowledgeItemJSON
	if err := json.Unmarshal(data, &raw); err != nil {
		return err
	}
	i.ID = raw.ID
	i.Category = raw.Category
	i.Question = raw.Question
	i.Answer = raw.Answer
	i.Keywords = raw.Keywords
	i.Covers = raw.Covers
	i.Source = raw.Source
	i.Embedding = raw.Embedding
	if raw.Enabled != nil {
		i.Enabled = *raw.Enabled
		i.EnabledSet = true
	} else {
		i.Enabled = false
		i.EnabledSet = false
	}
	return nil
}

func (i KnowledgeItem) TextForEmbedding() string {
	parts := []string{i.Category, i.Question, strings.Join(i.Keywords, " "), i.Answer}
	return strings.Join(parts, " ")
}

func (i KnowledgeItem) Validate() error {
	if strings.TrimSpace(i.ID) == "" {
		return errors.New("knowledge id is required")
	}
	if strings.TrimSpace(i.Category) == "" {
		return fmt.Errorf("knowledge %q category is required", i.ID)
	}
	if strings.TrimSpace(i.Question) == "" {
		return fmt.Errorf("knowledge %q question is required", i.ID)
	}
	if strings.TrimSpace(i.Answer) == "" {
		return fmt.Errorf("knowledge %q answer is required", i.ID)
	}
	if len(i.Keywords) == 0 {
		return fmt.Errorf("knowledge %q keywords are required", i.ID)
	}
	nonEmptyKeyword := false
	for _, keyword := range i.Keywords {
		if strings.TrimSpace(keyword) != "" {
			nonEmptyKeyword = true
			break
		}
	}
	if !nonEmptyKeyword {
		return fmt.Errorf("knowledge %q keywords are required", i.ID)
	}
	if !i.EnabledSet {
		return fmt.Errorf("knowledge %q enabled is required", i.ID)
	}
	if err := validateKnowledgeCovers(i.ID, i.Enabled, i.Covers); err != nil {
		return err
	}
	if !validKnowledgeSource(i.Source) {
		return fmt.Errorf("knowledge %q source %q is invalid", i.ID, i.Source)
	}
	return nil
}

func validateKnowledgeCovers(id string, enabled bool, covers []string) error {
	seen := make(map[string]struct{}, len(covers))
	for _, cover := range covers {
		cover = strings.TrimSpace(cover)
		if cover == "" {
			return fmt.Errorf("knowledge %q covers contain empty value", id)
		}
		if !IsAllowedCover(cover) {
			return fmt.Errorf("knowledge %q cover %q is invalid", id, cover)
		}
		if _, ok := seen[cover]; ok {
			return fmt.Errorf("knowledge %q cover %q is duplicated", id, cover)
		}
		seen[cover] = struct{}{}
	}
	if enabled && len(covers) == 0 {
		return fmt.Errorf("knowledge %q covers are required when enabled", id)
	}
	return nil
}

func validKnowledgeSource(source string) bool {
	switch strings.TrimSpace(source) {
	case SourceDemo, SourceUserConfirmed, SourcePublicGeneral:
		return true
	default:
		return false
	}
}

func LoadDir(dir string) ([]KnowledgeItem, error) {
	entries, err := os.ReadDir(dir)
	if err != nil {
		return nil, err
	}
	var files []string
	for _, entry := range entries {
		if entry.IsDir() || strings.ToLower(filepath.Ext(entry.Name())) != ".json" {
			continue
		}
		files = append(files, filepath.Join(dir, entry.Name()))
	}
	sort.Strings(files)

	var all []KnowledgeItem
	for _, file := range files {
		items, err := LoadFile(file)
		if err != nil {
			return nil, err
		}
		all = append(all, items...)
	}
	return all, validateUniqueIDs(all)
}

func LoadFile(path string) ([]KnowledgeItem, error) {
	data, err := os.ReadFile(path)
	if err != nil {
		return nil, err
	}
	items, err := decodeKnowledge(data)
	if err != nil {
		return nil, fmt.Errorf("load %s: %w", path, err)
	}
	for _, item := range items {
		if err := item.Validate(); err != nil {
			return nil, fmt.Errorf("load %s: %w", path, err)
		}
	}
	return items, nil
}

func decodeKnowledge(data []byte) ([]KnowledgeItem, error) {
	var items []KnowledgeItem
	if err := json.Unmarshal(data, &items); err == nil {
		return items, nil
	}
	var item KnowledgeItem
	if err := json.Unmarshal(data, &item); err != nil {
		return nil, err
	}
	return []KnowledgeItem{item}, nil
}

func validateUniqueIDs(items []KnowledgeItem) error {
	seen := make(map[string]struct{}, len(items))
	for _, item := range items {
		if _, ok := seen[item.ID]; ok {
			return fmt.Errorf("duplicate knowledge id %q", item.ID)
		}
		seen[item.ID] = struct{}{}
	}
	return nil
}

func enabledKnowledge(items []KnowledgeItem) []KnowledgeItem {
	enabled := make([]KnowledgeItem, 0, len(items))
	for _, item := range items {
		if item.Enabled {
			enabled = append(enabled, item)
		}
	}
	return enabled
}

func rejectActiveEmbeddings(items []KnowledgeItem) error {
	for _, item := range items {
		if len(item.Embedding) > 0 {
			return fmt.Errorf("active knowledge %q contains embedding; remove stored embedding for remote-http", item.ID)
		}
	}
	return nil
}
