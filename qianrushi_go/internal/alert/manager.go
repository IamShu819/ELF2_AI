package alert

import (
	"sync"

	"comm-gateway/internal/tts"
)

var warmVoiceMap = map[string]string{
	"1":   "检测到人员摔倒，请立即救援",
	"2":   "检测到车辆",
	"3":   "检测到火焰，请立即处理",
	"4":   "检测到水区异常，请立即处理",
	"sos": "检测到 SOS 求救，请立即处理",
}

var cmdVoiceMap = map[int]string{
	201: "舱门正在打开，请注意安全",
	202: "舱门正在关闭，请注意安全",
	301: "升降台正在打开，请注意安全",
	302: "升降台正在关闭，请注意安全",
}

type AlarmEvent struct {
	Code    string `json:"code"`
	Active  bool   `json:"active"`
	Message string `json:"message"`
}

type CmdEvent struct {
	SpeakId int    `json:"speakId"`
	Message string `json:"message"`
}

type OnAlarmFunc func(AlarmEvent)
type OnCmdEventFunc func(CmdEvent)

type Manager struct {
	synthFactory func() (*tts.Synthesizer, error)
	mu           sync.Mutex
	alarmActive  bool
	alarmVoice   string
	alarmStopCh  chan struct{}
	onAlarm      OnAlarmFunc
	onCmdEvent   OnCmdEventFunc
}

func NewManager(synthFactory func() (*tts.Synthesizer, error)) *Manager {
	return &Manager{
		synthFactory: synthFactory,
	}
}

func (m *Manager) OnAlarm(fn OnAlarmFunc) {
	m.mu.Lock()
	defer m.mu.Unlock()
	m.onAlarm = fn
}

func (m *Manager) OnCmdEvent(fn OnCmdEventFunc) {
	m.mu.Lock()
	defer m.mu.Unlock()
	m.onCmdEvent = fn
}

func (m *Manager) HandleWarm(code string) {
	m.mu.Lock()

	if code == "0" {
		m.alarmActive = false
		m.alarmVoice = ""
		if m.alarmStopCh != nil {
			close(m.alarmStopCh)
			m.alarmStopCh = nil
		}
		m.mu.Unlock()
		m.emitAlarm(AlarmEvent{Code: code, Active: false, Message: ""})
		return
	}

	message, ok := warmVoiceMap[code]
	if !ok {
		m.mu.Unlock()
		return
	}

	if m.alarmStopCh != nil {
		close(m.alarmStopCh)
		m.alarmStopCh = nil
	}
	m.alarmActive = true
	m.alarmVoice = message
	m.mu.Unlock()

	m.emitAlarm(AlarmEvent{Code: code, Active: true, Message: message})
}

func (m *Manager) HandleCmd(speakId int) {
	message, ok := cmdVoiceMap[speakId]
	if !ok {
		return
	}

	m.emitCmdEvent(CmdEvent{SpeakId: speakId, Message: message})
}

func (m *Manager) emitAlarm(ev AlarmEvent) {
	m.mu.Lock()
	fn := m.onAlarm
	m.mu.Unlock()
	if fn != nil {
		fn(ev)
	}
}

func (m *Manager) emitCmdEvent(ev CmdEvent) {
	m.mu.Lock()
	fn := m.onCmdEvent
	m.mu.Unlock()
	if fn != nil {
		fn(ev)
	}
}
