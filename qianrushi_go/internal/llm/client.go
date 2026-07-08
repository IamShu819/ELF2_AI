package llm

import (
	"bufio"
	"bytes"
	"context"
	"encoding/json"
	"fmt"
	"io"
	"log"
	"net/http"
	"strings"
	"time"
)

type Config struct {
	BaseURL     string
	Model       string
	Temperature float64
	MaxTokens   int
}

type Streamer interface {
	ChatStream(ctx context.Context, question string) (<-chan string, error)
}

type Client struct {
	httpClient *http.Client
	config     Config
}

func NewOpenAIClient(config Config) *Client {
	if config.BaseURL == "" {
		config.BaseURL = "http://192.168.31.12:8000/v1/chat/completions"
	}
	if config.Model == "" {
		config.Model = "local-model"
	}
	if config.Temperature == 0 {
		config.Temperature = 0.3
	}
	if config.MaxTokens == 0 {
		config.MaxTokens = 256
	}

	return &Client{
		httpClient: &http.Client{Timeout: 60 * time.Second},
		config:     config,
	}
}

func (c *Client) ChatStream(ctx context.Context, question string) (<-chan string, error) {
	return c.ChatStreamRequest(ctx, DefaultRequest(question))
}

func (c *Client) ChatStreamRequest(ctx context.Context, req Request) (<-chan string, error) {
	req = NewRequest(req.SystemPrompt, req.UserData)
	if req.UserData == "" {
		return nil, fmt.Errorf("empty question")
	}
	if req.SystemPrompt == "" {
		req.SystemPrompt = systemPrompt
	}

	payload := chatRequest{
		Model: c.config.Model,
		Messages: []message{
			{Role: "system", Content: req.SystemPrompt},
			{Role: "user", Content: req.UserData},
		},
		Temperature: c.config.Temperature,
		MaxTokens:   c.config.MaxTokens,
		Stream:      true,
	}

	body, err := json.Marshal(payload)
	if err != nil {
		return nil, err
	}

	httpReq, err := http.NewRequestWithContext(ctx, http.MethodPost, c.config.BaseURL, bytes.NewReader(body))
	if err != nil {
		return nil, err
	}
	httpReq.Header.Set("Content-Type", "application/json")
	httpReq.Header.Set("Accept", "text/event-stream")

	log.Printf("[llm] POST %s model=%s", c.config.BaseURL, c.config.Model)
	resp, err := c.httpClient.Do(httpReq)
	if err != nil {
		return nil, err
	}

	if resp.StatusCode < 200 || resp.StatusCode >= 300 {
		raw, _ := io.ReadAll(resp.Body)
		resp.Body.Close()
		return nil, fmt.Errorf("LLM HTTP %d: %s", resp.StatusCode, string(raw))
	}

	ch := make(chan string, 64)
	go c.readSSE(ctx, resp.Body, ch)
	return ch, nil
}

func (c *Client) readSSE(ctx context.Context, body io.ReadCloser, ch chan<- string) {
	defer body.Close()
	defer close(ch)

	scanner := bufio.NewScanner(body)
	scanner.Buffer(make([]byte, 0, 65536), 65536)

	for scanner.Scan() {
		line := scanner.Text()

		select {
		case <-ctx.Done():
			return
		default:
		}

		if line == "" {
			continue
		}

		if !strings.HasPrefix(line, "data: ") {
			continue
		}

		data := strings.TrimPrefix(line, "data: ")
		if data == "[DONE]" {
			return
		}

		var chunk streamChunk
		if err := json.Unmarshal([]byte(data), &chunk); err != nil {
			continue
		}

		if len(chunk.Choices) == 0 {
			continue
		}

		content := chunk.Choices[0].Delta.Content

		finishReason := chunk.Choices[0].FinishReason
		if finishReason != "" && content == "" {
			return
		}

		if content == "" {
			continue
		}

		select {
		case ch <- content:
		case <-ctx.Done():
			return
		}
	}
}

func (c *Client) Chat(ctx context.Context, question string) (string, error) {
	ch, err := c.ChatStream(ctx, question)
	if err != nil {
		return "", err
	}

	var builder strings.Builder
	for token := range ch {
		builder.WriteString(token)
	}
	result := strings.TrimSpace(builder.String())
	if result == "" {
		return "", fmt.Errorf("LLM response is empty")
	}
	return result, nil
}

type chatRequest struct {
	Model       string    `json:"model"`
	Messages    []message `json:"messages"`
	Temperature float64   `json:"temperature"`
	MaxTokens   int       `json:"max_tokens"`
	Stream      bool      `json:"stream"`
}

type message struct {
	Role    string `json:"role"`
	Content string `json:"content"`
}

type streamChunk struct {
	Choices []struct {
		Delta struct {
			Content string `json:"content"`
		} `json:"delta"`
		FinishReason string `json:"finish_reason"`
	} `json:"choices"`
}

const systemPrompt = `你是园区智能巡检助手，部署在RK3588边缘计算终端上，通过语音与巡检人员交互。

你的能力：
- 实时获取园区环境传感器数据（温度、湿度、光照、PM2.5、PM10、风速、风向）
- 接收并播报告警信息（人员摔倒、车辆入侵、火焰检测、水区异常）
- 播报设备控制指令（舱门开关、升降台升降）
- 回答巡检相关的安全规范和应急流程问题

你的行为准则：
- 回答必须简短口语化，像真人说话，每句话控制在20字以内
- 不使用Markdown、编号列表、项目符号、标题、括号注释
- 数字用中文口语表达，如"二十八点五度"而不是"28.5℃"
- 温度回答格式："当前温度XX度"，湿度："当前湿度百分之XX"
- 遇到告警类问题时，优先提醒注意安全
- 不确定的事情说"我这边没有收到相关数据"，不要编造
- 用户没问环境数据时，不要主动播报传感器数值
- 如果用户的问题和环境无关（如闲聊、安全规范、应急流程），正常简短回答即可

当用户问题后面附带了"当前环境数据"，说明这是传感器实时数据，请结合这些数据回答用户的环境相关问题。`
