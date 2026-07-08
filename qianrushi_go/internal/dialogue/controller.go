// Dialogue 对话管理包，控制人机对话的状态流转
package dialogue

import (
	"sync"
	"time"
)

// State 表示对话状态的字符串类型
type State string

const (
	// StateIdle 空闲状态，无人说话
	StateIdle State = "idle"
	// StateUserSpeaking 用户正在说话
	StateUserSpeaking State = "user_speaking"
	// StateAIThinking AI 正在思考/生成回复
	StateAIThinking State = "ai_thinking"
	// StateAISpeaking AI 正在播放语音回复
	StateAISpeaking State = "ai_speaking"
	// StateUserInterrupting 用户正在打断 AI 播放
	StateUserInterrupting State = "user_interrupting"
)

// Controller 对话状态控制器，管理用户与 AI 之间的对话状态转换
type Controller struct {
	mu          sync.Mutex
	state       State
	lastVoiceAt time.Time
}

// NewController 创建对话状态控制器，初始状态为空闲
func NewController() *Controller {
	return &Controller{state: StateIdle}
}

// State 返回当前对话状态
func (c *Controller) State() State {
	c.mu.Lock()
	defer c.mu.Unlock()
	return c.state
}

// OnUserAudio 处理用户音频输入，根据 VAD 和 ASR 结果更新状态
func (c *Controller) OnUserAudio(vadActive bool, asrText string, now time.Time) State {
	c.mu.Lock()
	defer c.mu.Unlock()

	if vadActive {
		c.lastVoiceAt = now
	}

	switch c.state {
	case StateIdle:
		// 检测到语音，进入用户说话状态
		if vadActive {
			c.state = StateUserSpeaking
		}
	case StateUserSpeaking:
		// 语音结束且静音超过 1.2 秒，转为 AI 思考状态
		if !vadActive && !c.lastVoiceAt.IsZero() && now.Sub(c.lastVoiceAt) > 1200*time.Millisecond {
			c.state = StateAIThinking
		}
	case StateAISpeaking:
		// AI 播放时用户说话且文本长度 >= 2，判定为打断
		if vadActive && len([]rune(asrText)) >= 2 {
			c.state = StateUserInterrupting
		}
	case StateUserInterrupting:
		// 打断后用户持续说话，转为用户说话状态
		if vadActive {
			c.state = StateUserSpeaking
		}
	}

	return c.state
}

// OnASREndpoint ASR 检测到语音结束时，将用户说话转为 AI 思考
func (c *Controller) OnASREndpoint() State {
	c.mu.Lock()
	defer c.mu.Unlock()
	if c.state == StateUserSpeaking {
		c.state = StateAIThinking
	}
	return c.state
}

// OnAIReplyReady AI 回复准备就绪，切换到 AI 说话状态
func (c *Controller) OnAIReplyReady() State {
	c.mu.Lock()
	defer c.mu.Unlock()
	c.state = StateAISpeaking
	return c.state
}

// OnClientTTSStarted 标记客户端已经开始播放 AI 语音。
func (c *Controller) OnClientTTSStarted() State {
	c.mu.Lock()
	defer c.mu.Unlock()
	if c.state == StateAIThinking || c.state == StateAISpeaking {
		c.state = StateAISpeaking
	}
	return c.state
}

// OnClientTTSStopped 标记客户端 AI 语音播放结束。
func (c *Controller) OnClientTTSStopped() State {
	c.mu.Lock()
	defer c.mu.Unlock()
	if c.state == StateAISpeaking {
		c.state = StateIdle
	}
	return c.state
}

// OnAIFinishedSpeaking AI 播放结束，回到空闲状态
func (c *Controller) OnAIFinishedSpeaking() State {
	c.mu.Lock()
	defer c.mu.Unlock()
	c.state = StateIdle
	return c.state
}

// OnInterrupted 检测到用户打断时，切换到打断状态
func (c *Controller) OnInterrupted() State {
	c.mu.Lock()
	defer c.mu.Unlock()
	c.state = StateUserInterrupting
	return c.state
}
