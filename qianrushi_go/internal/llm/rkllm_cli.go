package llm

import (
	"bytes"
	"context"
	"fmt"
	"os"
	"os/exec"
	"path/filepath"
	"regexp"
	"strconv"
	"strings"
	"sync"
	"time"
)

type RKLLMCLIConfig struct {
	BinaryPath      string
	ModelPath       string
	WorkDir         string
	MaxNewTokens    int
	MaxContext      int
	Timeout         time.Duration
	UsePromptPrefix bool
	StripThink      bool
}

type RKLLMCLIClient struct {
	config RKLLMCLIConfig
}

var rkllmCLIMu sync.Mutex

func NewRKLLMCLIClient(config RKLLMCLIConfig) *RKLLMCLIClient {
	if config.BinaryPath == "" {
		config.BinaryPath = "/home/elf/AI/llm_demo"
	}
	if config.ModelPath == "" {
		config.ModelPath = "/home/elf/AI/DeepSeek-R1-Distill-Qwen-1.5B_W8A8_RK3588.rkllm"
	}
	if config.WorkDir == "" {
		config.WorkDir = filepath.Dir(config.BinaryPath)
	}
	if config.MaxNewTokens == 0 {
		config.MaxNewTokens = 10000
	}
	if config.MaxContext == 0 {
		config.MaxContext = 10000
	}
	if config.Timeout == 0 {
		config.Timeout = 120 * time.Second
	}

	return &RKLLMCLIClient{config: config}
}

func (c *RKLLMCLIClient) ChatStream(ctx context.Context, question string) (<-chan string, error) {
	return c.ChatStreamRequest(ctx, DefaultRequest(question))
}

func (c *RKLLMCLIClient) ChatStreamRequest(ctx context.Context, req Request) (<-chan string, error) {
	answer, err := c.ChatRequest(ctx, req)
	if err != nil {
		return nil, err
	}

	ch := make(chan string, 8)
	go func() {
		defer close(ch)
		for _, part := range splitForStreaming(answer) {
			select {
			case ch <- part:
			case <-ctx.Done():
				return
			}
		}
	}()
	return ch, nil
}

func (c *RKLLMCLIClient) Chat(ctx context.Context, question string) (string, error) {
	return c.ChatRequest(ctx, DefaultRequest(question))
}

func (c *RKLLMCLIClient) ChatRequest(ctx context.Context, req Request) (string, error) {
	req = NewRequest(req.SystemPrompt, req.UserData)
	if req.UserData == "" {
		return "", fmt.Errorf("empty question")
	}
	question := req.UserData

	rkllmCLIMu.Lock()
	defer rkllmCLIMu.Unlock()

	timeoutCtx, cancel := context.WithTimeout(ctx, c.config.Timeout)
	defer cancel()

	args := []string{
		c.config.ModelPath,
		strconv.Itoa(c.config.MaxNewTokens),
		strconv.Itoa(c.config.MaxContext),
	}
	cmd := exec.CommandContext(timeoutCtx, c.config.BinaryPath, args...)
	cmd.Dir = c.config.WorkDir
	cmd.Env = append(os.Environ(), libraryPathEnv(c.config.BinaryPath, c.config.ModelPath, c.config.WorkDir))
	cmd.Stdin = strings.NewReader(c.promptForRequest(req) + "\n")

	var stdout bytes.Buffer
	var stderr bytes.Buffer
	cmd.Stdout = &stdout
	cmd.Stderr = &stderr

	err := cmd.Run()
	output := stdout.String()
	if stderr.Len() > 0 {
		output += "\n" + stderr.String()
	}

	if failure := rkllmFailureMessage(output); failure != "" {
		return "", fmt.Errorf("%s", failure)
	}

	answer := cleanRKLLMOutput(output, question, c.config.StripThink)
	if answer != "" {
		return answer, nil
	}
	if timeoutCtx.Err() != nil {
		return "", fmt.Errorf("rkllm cli timed out after %s", c.config.Timeout)
	}
	if err != nil {
		return "", fmt.Errorf("rkllm cli failed: %w: %s", err, strings.TrimSpace(output))
	}
	return "", fmt.Errorf("rkllm cli returned empty answer")
}

func rkllmFailureMessage(output string) string {
	lower := strings.ToLower(output)
	switch {
	case strings.Contains(lower, "rkllm init failed"),
		strings.Contains(lower, "rknn_init_fail"),
		strings.Contains(lower, "failed to load model"),
		strings.Contains(lower, "load model file error"),
		strings.Contains(lower, "failed to allocate handle"),
		strings.Contains(lower, "failed to malloc npu memory"),
		strings.Contains(lower, "out of memory"),
		strings.Contains(lower, "oom-killer"),
		strings.Contains(lower, "killed"),
		strings.Contains(lower, "permission denied"),
		strings.Contains(lower, "no such file or directory"):
		return "RKLLM 模型调用失败，请检查模型路径、运行库路径和 NPU 内存占用"
	default:
		return ""
	}
}

func (c *RKLLMCLIClient) promptFor(question string) string {
	return c.promptForRequest(DefaultRequest(question))
}

func (c *RKLLMCLIClient) promptForRequest(req Request) string {
	req = NewRequest(req.SystemPrompt, req.UserData)
	if req.SystemPrompt == "" {
		req.SystemPrompt = systemPrompt
	}
	if !c.config.UsePromptPrefix {
		return req.CombinedPrompt()
	}
	return req.SystemPrompt + "\n用户：" + req.UserData + "\n助手："
}

func libraryPathEnv(paths ...string) string {
	dirs := make([]string, 0, len(paths)+1)
	for _, path := range paths {
		if path == "" {
			continue
		}
		info, err := os.Stat(path)
		if err == nil && info.IsDir() {
			dirs = append(dirs, path)
			continue
		}
		dir := filepath.Dir(path)
		if dir != "." && dir != "" {
			dirs = append(dirs, dir)
		}
	}
	if existing := os.Getenv("LD_LIBRARY_PATH"); existing != "" {
		dirs = append(dirs, existing)
	}
	return "LD_LIBRARY_PATH=" + strings.Join(uniqueStrings(dirs), ":")
}

func uniqueStrings(values []string) []string {
	seen := make(map[string]struct{}, len(values))
	result := make([]string, 0, len(values))
	for _, value := range values {
		value = strings.TrimSpace(value)
		if value == "" {
			continue
		}
		if _, ok := seen[value]; ok {
			continue
		}
		seen[value] = struct{}{}
		result = append(result, value)
	}
	return result
}

func splitForStreaming(text string) []string {
	text = strings.TrimSpace(text)
	if text == "" {
		return nil
	}

	var parts []string
	var current strings.Builder
	for _, r := range text {
		current.WriteRune(r)
		switch r {
		case '。', '！', '？', '!', '?', '\n':
			parts = appendPartString(parts, current.String())
			current.Reset()
		}
	}
	parts = appendPartString(parts, current.String())
	if len(parts) == 0 {
		return []string{text}
	}
	return parts
}

func appendPartString(parts []string, text string) []string {
	text = strings.TrimSpace(text)
	if text == "" {
		return parts
	}
	return append(parts, text)
}

var ansiPattern = regexp.MustCompile(`\x1b\[[0-9;]*[A-Za-z]`)
var demoMenuIndexPattern = regexp.MustCompile(`^\[\d+\]`)
var leadingRolePattern = regexp.MustCompile(`(?i)^\s*(?:robot|assistant|user|bot|ai|助手|用户)\s*[:：]\s*`)
var trailingMarkerPattern = regexp.MustCompile(`\s*[<>{}\[\]|` + "`" + `]+[\s<>{}\[\]|` + "`" + `]*$`)

func cleanRKLLMOutput(output string, question string, stripThink bool) string {
	output = ansiPattern.ReplaceAllString(output, "")
	output = strings.ReplaceAll(output, "\r\n", "\n")
	output = strings.ReplaceAll(output, "\r", "\n")

	var lines []string
	for _, line := range strings.Split(output, "\n") {
		line = strings.TrimSpace(line)
		if line == "" || shouldDropRKLLMLine(line, question) {
			continue
		}
		lines = append(lines, line)
	}

	answer := strings.TrimSpace(strings.Join(lines, "\n"))
	if stripThink {
		answer = stripThinkBlocks(answer)
	}
	answer = strings.TrimSpace(answer)
	answer = stripRKLLMArtifacts(answer)
	return strings.TrimSpace(answer)
}

func stripRKLLMArtifacts(text string) string {
	text = strings.TrimSpace(text)
	text = truncateAtRKLLMStopToken(text)
	for {
		cleaned := leadingRolePattern.ReplaceAllString(text, "")
		if cleaned == text {
			break
		}
		text = strings.TrimSpace(cleaned)
	}
	text = strings.ReplaceAll(text, "<<<", "")
	text = strings.ReplaceAll(text, ">>>", "")
	text = trailingMarkerPattern.ReplaceAllString(text, "")
	return strings.TrimSpace(text)
}

func shouldDropRKLLMLine(line string, question string) bool {
	lower := strings.ToLower(line)
	trimmedQuestion := strings.TrimSpace(question)
	if trimmedQuestion != "" {
		switch strings.TrimSpace(line) {
		case trimmedQuestion,
			"用户：" + trimmedQuestion,
			"用户: " + trimmedQuestion,
			"User: " + trimmedQuestion,
			"user: " + trimmedQuestion:
			return true
		}
	}
	if strings.HasPrefix(lower, "rkllm init") ||
		strings.Contains(lower, "rknn_init_fail") ||
		strings.Contains(lower, "failed to allocate") ||
		strings.Contains(lower, "failed to malloc") ||
		strings.Contains(lower, "failed to load model") ||
		strings.Contains(lower, "load model file error") ||
		strings.Contains(lower, "bad address") ||
		strings.Contains(lower, "errno:") ||
		strings.HasPrefix(lower, "i rkllm:") ||
		strings.HasPrefix(lower, "e rknn:") ||
		strings.Contains(lower, "rkllm-runtime version") ||
		strings.Contains(lower, "rknpu driver version") ||
		strings.Contains(lower, "platform: rk3588") ||
		strings.Contains(lower, "model loaded") ||
		strings.Contains(lower, "llm model") ||
		strings.Contains(lower, "init success") ||
		strings.Contains(lower, "可输入以下问题") ||
		strings.Contains(line, "现有一笼子") ||
		strings.Contains(line, "有28位小朋友") ||
		strings.Contains(lower, "********") {
		return true
	}
	if demoMenuIndexPattern.MatchString(line) {
		return true
	}
	switch strings.TrimSpace(line) {
	case "用户：", "助手：", "User:", "Assistant:", ">":
		return true
	default:
		return false
	}
}

func stripThinkBlocks(text string) string {
	for {
		start := strings.Index(text, "<think>")
		if start < 0 {
			return strings.TrimSpace(text)
		}
		end := strings.Index(text[start:], "</think>")
		if end < 0 {
			return strings.TrimSpace(text[:start])
		}
		end += start + len("</think>")
		text = text[:start] + text[end:]
	}
}
