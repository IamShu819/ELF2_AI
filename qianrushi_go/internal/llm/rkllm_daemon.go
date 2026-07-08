package llm

import (
	"context"
	"fmt"
	"io"
	"log"
	"os"
	"os/exec"
	"strconv"
	"strings"
	"sync"
	"time"
)

type RKLLMDaemonClient struct {
	config RKLLMCLIConfig

	mu       sync.Mutex
	cmd      *exec.Cmd
	stdin    io.WriteCloser
	outputCh chan string
	done     chan error
}

func NewRKLLMDaemonClient(config RKLLMCLIConfig) *RKLLMDaemonClient {
	cli := NewRKLLMCLIClient(config)
	return &RKLLMDaemonClient{config: cli.config}
}

func (c *RKLLMDaemonClient) Warmup(ctx context.Context) error {
	c.mu.Lock()
	defer c.mu.Unlock()
	if err := c.ensureStartedLocked(ctx); err != nil {
		c.stopLocked()
		return err
	}
	return nil
}

func (c *RKLLMDaemonClient) ChatStream(ctx context.Context, question string) (<-chan string, error) {
	return c.ChatStreamRequest(ctx, DefaultRequest(question))
}

func (c *RKLLMDaemonClient) ChatStreamRequest(ctx context.Context, req Request) (<-chan string, error) {
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

func (c *RKLLMDaemonClient) Chat(ctx context.Context, question string) (string, error) {
	return c.ChatRequest(ctx, DefaultRequest(question))
}

func (c *RKLLMDaemonClient) ChatRequest(ctx context.Context, req Request) (string, error) {
	req = NewRequest(req.SystemPrompt, req.UserData)
	if req.UserData == "" {
		return "", fmt.Errorf("empty question")
	}
	question := req.UserData

	c.mu.Lock()
	defer c.mu.Unlock()

	if err := c.ensureStartedLocked(ctx); err != nil {
		c.stopLocked()
		return "", err
	}
	c.drainOutputLocked()

	sentinel := fmt.Sprintf("QIANRUSHI_DONE_%d", time.Now().UnixNano())
	prompt := c.daemonPromptForRequest(req, sentinel)
	if _, err := io.WriteString(c.stdin, prompt+"\n"); err != nil {
		c.stopLocked()
		return "", fmt.Errorf("rkllm daemon write failed: %w", err)
	}

	raw, restartDaemon, err := c.readAnswerLocked(ctx, question, sentinel)
	if restartDaemon {
		defer c.stopLocked()
	}
	if err != nil {
		c.stopLocked()
		return "", err
	}
	if failure := rkllmFailureMessage(raw); failure != "" {
		return "", fmt.Errorf("%s", failure)
	}

	answer := cleanRKLLMOutput(stripSentinel(raw, sentinel), question, c.config.StripThink)
	if answer == "" {
		return "", fmt.Errorf("rkllm daemon returned empty answer")
	}
	return answer, nil
}

func (c *RKLLMDaemonClient) ensureStartedLocked(ctx context.Context) error {
	if c.cmd != nil {
		select {
		case err := <-c.done:
			c.stopLocked()
			if err != nil {
				return fmt.Errorf("rkllm daemon exited: %w", err)
			}
			return fmt.Errorf("rkllm daemon exited")
		default:
			return nil
		}
	}

	args := []string{
		c.config.ModelPath,
		strconv.Itoa(c.config.MaxNewTokens),
		strconv.Itoa(c.config.MaxContext),
	}
	cmd := exec.Command(c.config.BinaryPath, args...)
	cmd.Dir = c.config.WorkDir
	cmd.Env = append(os.Environ(), libraryPathEnv(c.config.BinaryPath, c.config.ModelPath, c.config.WorkDir))

	stdin, err := cmd.StdinPipe()
	if err != nil {
		return err
	}
	stdout, err := cmd.StdoutPipe()
	if err != nil {
		return err
	}
	stderr, err := cmd.StderrPipe()
	if err != nil {
		return err
	}

	if err := cmd.Start(); err != nil {
		return err
	}

	c.cmd = cmd
	c.stdin = stdin
	outputCh := make(chan string, 256)
	done := make(chan error, 1)
	c.outputCh = outputCh
	c.done = done

	var readers sync.WaitGroup
	readPipe := func(r io.Reader) {
		defer readers.Done()
		buf := make([]byte, 4096)
		for {
			n, err := r.Read(buf)
			if n > 0 {
				outputCh <- string(buf[:n])
			}
			if err != nil {
				return
			}
		}
	}
	readers.Add(2)
	go readPipe(stdout)
	go readPipe(stderr)
	go func() {
		err := cmd.Wait()
		readers.Wait()
		close(outputCh)
		done <- err
	}()

	log.Printf("[rkllm-daemon] starting %s model=%s", c.config.BinaryPath, c.config.ModelPath)
	return c.waitReadyLocked(ctx)
}

func (c *RKLLMDaemonClient) waitReadyLocked(ctx context.Context) error {
	timeout := time.NewTimer(c.config.Timeout)
	defer timeout.Stop()

	var raw strings.Builder
	for {
		select {
		case <-ctx.Done():
			return ctx.Err()
		case <-timeout.C:
			return fmt.Errorf("rkllm daemon init timed out after %s", c.config.Timeout)
		case err := <-c.done:
			if err != nil {
				return fmt.Errorf("rkllm daemon init failed: %w: %s", err, strings.TrimSpace(raw.String()))
			}
			return fmt.Errorf("rkllm daemon exited during init: %s", strings.TrimSpace(raw.String()))
		case chunk, ok := <-c.outputCh:
			if !ok {
				return fmt.Errorf("rkllm daemon output closed during init")
			}
			raw.WriteString(chunk)
			output := raw.String()
			if failure := rkllmFailureMessage(output); failure != "" {
				return fmt.Errorf("%s", failure)
			}
			if strings.Contains(output, "rkllm init success") || strings.Contains(output, "可输入以下问题") {
				log.Printf("[rkllm-daemon] ready")
				return nil
			}
		}
	}
}

var rkllmStopTokens = []string{
	"<｜endofsentence｜>",
	"endofsentence｜>",
	"<｜begin▁of▁sentence｜>",
	"<｜beginofsentence｜",
	"begin▁of▁sentence",
	"beginofsentence",
	"<｜User｜>",
	"<｜user｜>",
	"<｜user:",
	"<｜Assistant｜>",
	"<｜assistant｜>",
	"<｜Human｜>",
	"<｜human｜>",
	"<|endoftext|>",
	"User:",
	"user:",
	"Assistant:",
	"assistant:",
	"Human:",
	"human:",
	"用户：",
	"用户:",
	"助手：",
	"助手:",
}

func truncateAtRKLLMStopToken(text string) string {
	earliest := len(text)
	lowerText := strings.ToLower(text)
	for _, token := range rkllmStopTokens {
		if idx := strings.Index(lowerText, strings.ToLower(token)); idx >= 0 && idx < earliest {
			earliest = idx
		}
	}
	return text[:earliest]
}

func (c *RKLLMDaemonClient) readAnswerLocked(ctx context.Context, question string, sentinel string) (string, bool, error) {
	timeout := time.NewTimer(c.config.Timeout)
	defer timeout.Stop()

	idle := time.NewTimer(1500 * time.Millisecond)
	if !idle.Stop() {
		<-idle.C
	}

	var raw strings.Builder
	seenContent := false
	for {
		select {
		case <-ctx.Done():
			return raw.String(), false, ctx.Err()
		case <-timeout.C:
			return raw.String(), false, fmt.Errorf("rkllm daemon timed out after %s", c.config.Timeout)
		case <-idle.C:
			if seenContent {
				return raw.String(), false, nil
			}
		case err := <-c.done:
			if err != nil {
				return raw.String(), false, fmt.Errorf("rkllm daemon exited: %w", err)
			}
			return raw.String(), false, fmt.Errorf("rkllm daemon exited")
		case chunk, ok := <-c.outputCh:
			if !ok {
				return raw.String(), false, fmt.Errorf("rkllm daemon output closed")
			}
			raw.WriteString(chunk)
			output := raw.String()

			// 遇到 stop token 立即截断，不再等待 sentinel
			if truncated := truncateAtRKLLMStopToken(output); truncated != output {
				return truncated, true, nil
			}

			if strings.Contains(output, sentinel) {
				return output, true, nil
			}
			if failure := rkllmFailureMessage(output); failure != "" {
				return output, false, fmt.Errorf("%s", failure)
			}
			if cleanRKLLMOutput(output, question, c.config.StripThink) != "" {
				seenContent = true
				resetTimer(idle, 1500*time.Millisecond)
			}
		}
	}
}

func (c *RKLLMDaemonClient) daemonPromptFor(question string, sentinel string) string {
	return c.daemonPromptForRequest(DefaultRequest(question), sentinel)
}

func (c *RKLLMDaemonClient) daemonPromptForRequest(req Request, sentinel string) string {
	req = NewRequest(req.SystemPrompt, req.UserData)
	if req.SystemPrompt == "" {
		req.SystemPrompt = systemPrompt
	}
	prompt := req.CombinedPrompt()
	if c.config.UsePromptPrefix {
		prompt = req.SystemPrompt + " 用户：" + req.UserData + " 助手："
	}
	prompt = strings.Join(strings.Fields(prompt), " ")
	return prompt + " 请直接给出简短中文回答，不要输出角色名、标签、尖括号或推理过程。\n" + sentinel
}

func (c *RKLLMDaemonClient) drainOutputLocked() {
	for {
		select {
		case <-c.outputCh:
		default:
			return
		}
	}
}

func (c *RKLLMDaemonClient) stopLocked() {
	if c.stdin != nil {
		_ = c.stdin.Close()
		c.stdin = nil
	}
	if c.cmd != nil && c.cmd.Process != nil {
		_ = c.cmd.Process.Kill()
	}
	c.cmd = nil
	c.outputCh = nil
	c.done = nil
}

func resetTimer(timer *time.Timer, duration time.Duration) {
	if !timer.Stop() {
		select {
		case <-timer.C:
		default:
		}
	}
	timer.Reset(duration)
}

func stripSentinel(text string, sentinel string) string {
	text = strings.ReplaceAll(text, sentinel, "")
	text = strings.ReplaceAll(text, "回答结束后单独输出："+sentinel, "")
	text = strings.ReplaceAll(text, "回答结束后单独输出: "+sentinel, "")
	return strings.TrimSpace(text)
}
