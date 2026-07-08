package websocket

import (
	"context"
	"encoding/binary"
	"encoding/json"
	"errors"
	"fmt"
	"log"
	"math"
	"net/http"
	"regexp"
	"sort"
	"strings"
	"sync"
	"sync/atomic"
	"time"

	"comm-gateway/internal/asr"
	"comm-gateway/internal/dialogue"
	"comm-gateway/internal/llm"
	"comm-gateway/internal/rag"
	"comm-gateway/internal/tts"
	"comm-gateway/internal/vad"

	"github.com/gorilla/websocket"
)

const inputSampleRate = 16000
const audioFlagTTSPlaying uint32 = 1

var leadingListMarkerPattern = regexp.MustCompile(`^\s*(?:[-*•]\s+|\d+[.)、]\s*)`)
var speechRolePattern = regexp.MustCompile(`(?i)^\s*(?:robot|assistant|user|bot|ai|助手|用户)\s*[:：]\s*`)
var speechMarkupPattern = regexp.MustCompile(`[<>{}\[\]|` + "`" + `*_#~]+`)

type Config struct {
	RecognizerFactory  func() (*asr.StreamingRecognizer, error)
	SynthesizerFactory func() (*tts.Synthesizer, error)
	VADFactory         func() (*vad.Detector, error)
	LLM                llm.Streamer
	EnvProvider        func() map[string]interface{}
	EnvStore           *EnvStore
	RAG                *rag.Runtime
}

type Handler struct {
	config   Config
	upgrader websocket.Upgrader
	mu       sync.Mutex
	sessions map[*session]struct{}
}

func NewHandler(config Config) *Handler {
	return &Handler{
		config:   config,
		sessions: make(map[*session]struct{}),
		upgrader: websocket.Upgrader{
			CheckOrigin: func(*http.Request) bool { return true },
		},
	}
}

func (h *Handler) ServeHTTP(w http.ResponseWriter, r *http.Request) {
	conn, err := h.upgrader.Upgrade(w, r, nil)
	if err != nil {
		log.Printf("websocket upgrade failed: %v", err)
		return
	}

	session, err := h.newSession(conn)
	if err != nil {
		_ = conn.WriteJSON(event{Type: "error", Message: err.Error()})
		_ = conn.Close()
		return
	}
	session.run()
}

func (h *Handler) newSession(conn *websocket.Conn) (*session, error) {
	recognizer, err := h.config.RecognizerFactory()
	if err != nil {
		return nil, err
	}

	synthesizer, err := h.config.SynthesizerFactory()
	if err != nil {
		recognizer.Close()
		return nil, err
	}

	detector, err := h.config.VADFactory()
	if err != nil {
		recognizer.Close()
		synthesizer.Close()
		return nil, err
	}
	warmupVoiceEngines(recognizer, detector)

	ctx, cancel := context.WithCancel(context.Background())
	return &session{
		conn:        conn,
		recognizer:  recognizer,
		synthesizer: synthesizer,
		detector:    detector,
		llm:         h.config.LLM,
		envProvider: h.config.EnvProvider,
		envStore:    h.config.EnvStore,
		ragRuntime:  h.config.RAG,
		controller:  dialogue.NewController(),
		ctx:         ctx,
		cancel:      cancel,
		done:        make(chan struct{}),
		handler:     h,
	}, nil
}

func warmupVoiceEngines(recognizer *asr.StreamingRecognizer, detector *vad.Detector) {
	start := time.Now()
	silence := make([]float32, inputSampleRate/10)
	_ = recognizer.AcceptAudio(silence, inputSampleRate)
	recognizer.Reset()

	vadWindow := make([]float32, 512)
	_ = detector.IsActive(vadWindow, inputSampleRate)
	log.Printf("voice ASR/VAD warmup finished in %s", time.Since(start).Round(time.Millisecond))
}

type session struct {
	conn        *websocket.Conn
	writeMu     sync.Mutex
	recognizer  *asr.StreamingRecognizer
	synthesizer *tts.Synthesizer
	detector    *vad.Detector
	llm         llm.Streamer
	envProvider func() map[string]interface{}
	envStore    *EnvStore
	ragRuntime  *rag.Runtime
	envMu       sync.RWMutex
	envSnapshot map[string]interface{}
	controller  *dialogue.Controller
	ctx         context.Context
	cancel      context.CancelFunc
	lastText    string
	speakingID  atomic.Int64
	clientTTS   atomic.Bool
	responding  atomic.Bool
	done        chan struct{}
	closeOnce   sync.Once
	bgWg        sync.WaitGroup
	handler     *Handler
}

func (h *Handler) registerSession(s *session) {
	h.mu.Lock()
	h.sessions[s] = struct{}{}
	h.mu.Unlock()
}

func (h *Handler) unregisterSession(s *session) {
	h.mu.Lock()
	delete(h.sessions, s)
	h.mu.Unlock()
}

func (h *Handler) BroadcastSTM32Command(command string) int {
	if strings.TrimSpace(command) == "" {
		log.Printf("[voice] drop empty stm32 command")
		return 0
	}

	h.mu.Lock()
	sessions := make([]*session, 0, len(h.sessions))
	for s := range h.sessions {
		sessions = append(sessions, s)
	}
	h.mu.Unlock()

	if len(sessions) == 0 {
		log.Printf("[voice] no Qt voice client online for stm32 command")
		return 0
	}

	sent := 0
	for _, s := range sessions {
		if err := s.sendJSON(event{Type: "stm32_command", Command: command}); err != nil {
			log.Printf("[voice] stm32 command send failed: %v", err)
			if s.handler != nil {
				s.handler.unregisterSession(s)
			}
			continue
		}
		sent++
	}
	log.Printf("[voice] forwarded stm32 command to %d/%d Qt client(s), bytes=%d", sent, len(sessions), len(command))
	return sent
}

func (s *session) run() {
	if s.handler != nil {
		s.handler.registerSession(s)
		defer s.handler.unregisterSession(s)
	}
	defer s.close()
	s.sendJSON(event{Type: "state", State: string(s.controller.State())})

	for {
		messageType, payload, err := s.conn.ReadMessage()
		if err != nil {
			return
		}

		switch messageType {
		case websocket.BinaryMessage:
			if err := s.handleAudio(payload); err != nil {
				s.sendJSON(event{Type: "error", Message: err.Error()})
			}
		case websocket.TextMessage:
			s.handleControl(payload)
		}
	}
}

func (s *session) handleAudio(payload []byte) error {
	frame, err := decodeAudioFrame(payload)
	if err != nil {
		return err
	}

	s.clientTTS.Store(frame.ttsPlaying())

	active := s.detector.IsActive(frame.samples, frame.sampleRate)
	text := strings.TrimSpace(s.recognizer.AcceptAudio(frame.samples, frame.sampleRate))
	if text != "" && text != s.lastText {
		s.lastText = text
		s.sendJSON(event{Type: "asr_partial", Text: text})
	}

	state := s.controller.OnUserAudio(active, text, time.Now())
	if s.recognizer.IsEndpoint() {
		state = s.controller.OnASREndpoint()
	}
	s.sendJSON(event{Type: "state", State: string(state)})

	switch state {
	case dialogue.StateAIThinking:
		finalText := s.lastText
		s.lastText = ""
		s.recognizer.Reset()
		if finalText == "" {
			s.controller.OnAIFinishedSpeaking()
			return nil
		}
		s.sendJSON(event{Type: "asr_final", Text: finalText})
		log.Printf("[voice] asr final: %q", finalText)
		s.startReply(finalText)
	case dialogue.StateUserInterrupting:
		s.speakingID.Add(1)
		s.responding.Store(false)
		s.sendJSON(event{Type: "control", Command: "stop"})
		s.recognizer.Reset()
		s.controller.OnInterrupted()
	}

	return nil
}

func (s *session) handleControl(payload []byte) {
	var msg controlMessage
	if err := json.Unmarshal(payload, &msg); err != nil {
		s.sendJSON(event{Type: "error", Message: "invalid control json"})
		return
	}

	switch msg.Type {
	case "stop":
		s.speakingID.Add(1)
		s.clientTTS.Store(false)
		s.responding.Store(false)
		s.controller.OnInterrupted()
		s.sendJSON(event{Type: "control", Command: "stop"})
	case "finish":
		s.finishUtterance()
	case "tts_start":
		s.clientTTS.Store(true)
		state := s.controller.OnClientTTSStarted()
		s.sendJSON(event{Type: "state", State: string(state)})
	case "tts_stop":
		s.clientTTS.Store(false)
		if !s.responding.Load() {
			state := s.controller.OnClientTTSStopped()
			s.sendJSON(event{Type: "state", State: string(state)})
		}
	case "tool_result":
		s.speakToolResult(msg.Text)
	case "env_snapshot":
		s.setEnvSnapshot(msg.Data)
	case "reset":
		s.speakingID.Add(1)
		s.clientTTS.Store(false)
		s.responding.Store(false)
		s.recognizer.Reset()
		s.lastText = ""
		s.controller.OnAIFinishedSpeaking()
		s.sendJSON(event{Type: "state", State: string(s.controller.State())})
	}
}

func (s *session) finishUtterance() {
	finalText := strings.TrimSpace(s.lastText)
	s.lastText = ""
	s.recognizer.Reset()
	if finalText == "" {
		state := s.controller.OnAIFinishedSpeaking()
		s.sendJSON(event{Type: "error", Message: "没有识别到语音，请靠近麦克风重试"})
		s.sendJSON(event{Type: "state", State: string(state)})
		return
	}

	s.sendJSON(event{Type: "asr_final", Text: finalText})
	log.Printf("[voice] asr final: %q", finalText)
	state := s.controller.OnASREndpoint()
	s.sendJSON(event{Type: "state", State: string(state)})
	s.startReply(finalText)
}

func (s *session) startReply(question string) {
	s.bgWg.Add(1)
	go func() {
		defer s.bgWg.Done()
		s.replyStream(question)
	}()
}

type replyRoute int

const (
	replyRouteMap replyRoute = iota
	replyRouteLocal
	replyRouteRAGNoAnswer
	replyRouteRAG
	replyRouteChat
)

type replyPlan struct {
	kind          replyRoute
	mapIntent     mapToolIntent
	localText     string
	modelQuestion string
	llmRequest    llm.Request
}

func (s *session) planReply(ctx context.Context, question string) (replyPlan, error) {
	if intent, ok := detectMapToolIntent(question); ok {
		return replyPlan{kind: replyRouteMap, mapIntent: intent}, nil
	}
	if answer, ok := s.localEnvAnswer(question); ok {
		return replyPlan{kind: replyRouteLocal, localText: answer}, nil
	}
	if rag.IsDomainFactQuestion(question) {
		assessment, referenceContext, ok, err := s.retrieveRAG(ctx, question)
		if err != nil {
			return replyPlan{}, err
		}
		if !ok || !assessment.Answerable || len(assessment.EligibleResults) == 0 {
			return replyPlan{kind: replyRouteRAGNoAnswer, localText: rag.NoAnswerText}, nil
		}
		return replyPlan{kind: replyRouteRAG, llmRequest: rag.BuildLLMRequest(referenceContext, question)}, nil
	}
	return replyPlan{kind: replyRouteChat, modelQuestion: s.modelQuestion(question)}, nil
}

func (s *session) replyStream(question string) {
	log.Printf("[llm] request: %q", question)
	ctx, cancel := context.WithTimeout(s.ctx, 60*time.Second)
	defer cancel()

	plan, err := s.planReply(ctx, question)
	if err != nil {
		log.Printf("[rag] retrieve failed: %v", err)
		s.replyLocalText(rag.NoAnswerText)
		return
	}
	switch plan.kind {
	case replyRouteMap:
		s.callMapTool(question, plan.mapIntent)
		return
	case replyRouteLocal:
		s.replyLocalText(plan.localText)
		return
	case replyRouteRAGNoAnswer:
		s.replyLocalText(rag.NoAnswerText)
		return
	}

	var tokenCh <-chan string
	if plan.kind == replyRouteRAG {
		tokenCh, err = llm.StreamRequest(ctx, s.llm, plan.llmRequest)
	} else {
		tokenCh, err = s.llm.ChatStream(ctx, plan.modelQuestion)
	}
	if err != nil {
		log.Printf("[llm] request failed: %v", err)
		s.sendJSON(event{Type: "llm_stream_end"})
		s.sendJSON(event{Type: "error", Message: err.Error()})
		s.controller.OnAIFinishedSpeaking()
		s.sendJSON(event{Type: "state", State: string(s.controller.State())})
		return
	}

	id := s.speakingID.Add(1)
	s.responding.Store(true)
	defer s.responding.Store(false)
	ttsJobs := make(chan string, 16)
	var ttsWg sync.WaitGroup
	ttsWg.Add(1)
	go func() {
		defer ttsWg.Done()
		for part := range ttsJobs {
			if id != s.speakingID.Load() {
				continue
			}
			s.synthesizeAndSend(part, id)
		}
	}()
	defer func() {
		if ttsJobs != nil {
			close(ttsJobs)
			ttsWg.Wait()
		}
	}()

	queueTTS := func(part string) bool {
		if id != s.speakingID.Load() {
			return false
		}
		select {
		case ttsJobs <- part:
			return true
		case <-s.done:
			return false
		}
	}

	s.sendJSON(event{Type: "llm_stream_start"})

	state := s.controller.OnAIReplyReady()
	s.sendJSON(event{Type: "state", State: string(state)})

	var buf strings.Builder
	var fullAnswer strings.Builder
	var displayedAnswer strings.Builder
	ttsStarted := false
	firstLLMToken := true
	for token := range tokenCh {
		if id != s.speakingID.Load() {
			return
		}
		if firstLLMToken {
			log.Printf("[llm] first chunk: %q", token)
			firstLLMToken = false
		}

		buf.WriteString(token)
		fullAnswer.WriteString(token)

		parts := extractTTSChunks(&buf, !ttsStarted)
		for _, part := range parts {
			if id != s.speakingID.Load() {
				return
			}

			if !queueTTS(part) {
				return
			}
			displayedAnswer.WriteString(part)
			s.sendJSON(event{Type: "llm_delta", Text: displayedAnswer.String()})
			ttsStarted = true
		}
	}

	if id != s.speakingID.Load() {
		return
	}

	remainder := strings.TrimSpace(buf.String())
	if remainder != "" {
		for _, part := range extractTTSChunksFromText(remainder, !ttsStarted) {
			if id != s.speakingID.Load() {
				return
			}
			if !queueTTS(part) {
				return
			}
			displayedAnswer.WriteString(part)
			s.sendJSON(event{Type: "llm_delta", Text: displayedAnswer.String()})
			ttsStarted = true
		}
	}

	close(ttsJobs)
	ttsWg.Wait()
	ttsJobs = nil

	finalAnswer := strings.TrimSpace(fullAnswer.String())
	log.Printf("[llm] finished, runes=%d, text=%q", len([]rune(finalAnswer)), finalAnswer)
	s.sendJSON(event{Type: "llm_stream_end", Text: finalAnswer})

	if id == s.speakingID.Load() && !s.clientTTS.Load() {
		state = s.controller.OnAIFinishedSpeaking()
		s.sendJSON(event{Type: "state", State: string(state)})
	}
}

func (s *session) replyLocalText(answer string) {
	answer = strings.TrimSpace(answer)
	if answer == "" {
		return
	}

	id := s.speakingID.Add(1)
	s.responding.Store(true)
	defer s.responding.Store(false)

	s.sendJSON(event{Type: "llm_stream_start"})
	state := s.controller.OnAIReplyReady()
	s.sendJSON(event{Type: "state", State: string(state)})
	s.sendJSON(event{Type: "llm_delta", Text: answer})
	s.synthesizeAndSend(answer, id)
	s.sendJSON(event{Type: "llm_stream_end", Text: answer})
	if id == s.speakingID.Load() && !s.clientTTS.Load() {
		state = s.controller.OnAIFinishedSpeaking()
		s.sendJSON(event{Type: "state", State: string(state)})
	}
}

type mapToolIntent struct {
	action string
}

func (s *session) callMapTool(question string, intent mapToolIntent) {
	id := s.speakingID.Add(1)
	s.responding.Store(true)
	defer s.responding.Store(false)

	state := s.controller.OnAIReplyReady()
	s.sendJSON(event{Type: "state", State: string(state)})
	if id != s.speakingID.Load() {
		return
	}
	s.sendJSON(event{Type: "map_tool_call", Text: question, Command: intent.action})
}

func (s *session) speakToolResult(text string) {
	text = strings.TrimSpace(text)
	if text == "" {
		return
	}

	id := s.speakingID.Add(1)
	s.responding.Store(true)
	defer s.responding.Store(false)

	state := s.controller.OnAIReplyReady()
	s.sendJSON(event{Type: "state", State: string(state)})
	s.synthesizeAndSend(text, id)
	if id == s.speakingID.Load() && !s.clientTTS.Load() {
		state = s.controller.OnAIFinishedSpeaking()
		s.sendJSON(event{Type: "state", State: string(state)})
	}
}

func extractTTSChunks(buf *strings.Builder, firstChunk bool) []string {
	minWeakBoundaryRunes := 8
	maxPendingRunes := 36
	if firstChunk {
		minWeakBoundaryRunes = 5
		maxPendingRunes = 24
	}

	full := buf.String()
	if full == "" {
		return nil
	}

	runes := []rune(full)
	runesLen := len(runes)

	strongBoundary := -1
	weakBoundary := -1
	fallbackBoundary := -1
	for i, r := range runes {
		switch r {
		case '。', '！', '？', '.', '!', '?', '\n':
			if isSentenceEnd(runes, i) {
				strongBoundary = i
			}
		case '，', ',', '；', ';', '、', '：', ':':
			if i+1 >= minWeakBoundaryRunes {
				weakBoundary = i
			}
		default:
			if i+1 >= minWeakBoundaryRunes && isSoftSpeechBoundary(r) {
				fallbackBoundary = i
			}
		}
	}

	var cutEnd int
	if strongBoundary >= 0 {
		cutEnd = strongBoundary + 1
	} else if weakBoundary >= 0 && runesLen >= maxPendingRunes {
		cutEnd = weakBoundary + 1
	} else if fallbackBoundary >= 0 && runesLen >= maxPendingRunes {
		cutEnd = fallbackBoundary + 1
	} else {
		return nil
	}

	complete := string(runes[:cutEnd])
	remainder := string(runes[cutEnd:])

	buf.Reset()
	buf.WriteString(remainder)

	var parts []string
	for _, p := range splitSentences(complete) {
		p = strings.TrimSpace(p)
		if p != "" {
			parts = append(parts, p)
		}
	}
	return parts
}

func extractTTSChunksFromText(text string, firstChunk bool) []string {
	var parts []string
	var buf strings.Builder
	buf.WriteString(text)
	for {
		chunks := extractTTSChunks(&buf, firstChunk && len(parts) == 0)
		if len(chunks) == 0 {
			break
		}
		parts = append(parts, chunks...)
	}
	if remaining := strings.TrimSpace(buf.String()); remaining != "" {
		parts = append(parts, remaining)
	}
	return parts
}

func (s *session) synthesizeAndSend(text string, id int64) {
	text = normalizeSpeechText(text)
	if text == "" {
		return
	}

	start := time.Now()
	chunkCount := 0
	sampleCount := 0
	firstChunkAt := time.Time{}
	err := s.synthesizer.SynthesizeStream(text, func(audio *tts.Audio) bool {
		if id != s.speakingID.Load() {
			return false
		}
		if firstChunkAt.IsZero() {
			firstChunkAt = time.Now()
			log.Printf("TTS stream first chunk in %s, runes=%d, samples=%d, text=%q",
				firstChunkAt.Sub(start).Round(time.Millisecond), len([]rune(text)), len(audio.Samples), text)
		}
		chunkCount++
		sampleCount += len(audio.Samples)
		s.sendAudio(audio)
		return id == s.speakingID.Load()
	})
	if err != nil {
		log.Printf("TTS synthesis failed: %v", err)
		s.sendJSON(event{Type: "tts_error", Message: err.Error()})
		return
	}
	log.Printf("TTS stream finished in %s, runes=%d, chunks=%d, samples=%d, text=%q",
		time.Since(start).Round(time.Millisecond), len([]rune(text)), chunkCount, sampleCount, text)
}

func (s *session) retrieveRAG(ctx context.Context, question string) (rag.Answerability, string, bool, error) {
	if s.ragRuntime == nil || !s.ragRuntime.Enabled() {
		return rag.Answerability{Answerable: false, DecisionReason: "runtime_disabled"}, "", false, nil
	}
	return s.ragRuntime.RetrieveAnswerable(ctx, question)
}

const (
	ProductionRouteMap      = "map"
	ProductionRouteLocalEnv = "local_env"
	ProductionRouteRAG      = "rag"
	ProductionRouteChat     = "chat"
)

func ProductionRoute(question string) string {
	if _, ok := detectMapToolIntent(question); ok {
		return ProductionRouteMap
	}
	if isEnvQuestion(question) {
		return ProductionRouteLocalEnv
	}
	if rag.IsDomainFactQuestion(question) {
		return ProductionRouteRAG
	}
	return ProductionRouteChat
}

func (s *session) modelQuestion(question string) string {
	question = strings.TrimSpace(question)
	env := s.latestEnv()
	if len(env) == 0 {
		return question
	}
	return question + "\n\n当前环境数据：" + formatEnvData(env) + "。如果用户询问环境、温湿度、空气、光照、风速或风向，请结合这些数据回答；否则正常简短回答。"
}

func (s *session) localEnvAnswer(question string) (string, bool) {
	if !isEnvQuestion(question) {
		return "", false
	}
	env := s.latestEnv()
	if len(env) == 0 {
		return "当前还没有收到环境传感器数据，请检查 STM32 串口连接。", true
	}
	if containsAny(question, "pm2.5", "pm 2.5", "pm25") {
		return envValueAnswer(env, []string{"Enviroment_Pm25", "PM25", "pm25", "pm2_5", "pm2.5"}, "当前 PM2.5", "微克每立方米"), true
	}
	if containsAny(question, "pm10", "pm 10") {
		return envValueAnswer(env, []string{"Enviroment_Pm10", "PM10", "pm10"}, "当前 PM10", "微克每立方米"), true
	}
	if containsAny(question, "风速") && containsAny(question, "风向") {
		return envCombinedAnswer(env), true
	}
	if containsAny(question, "湿度") {
		return envValueAnswer(env, []string{"Enviroment_Humidity", "humidity", "Humidity", "air_humidity", "Air_Humidity", "湿度"}, "当前空气湿度", "%"), true
	}
	if containsAny(question, "温度") {
		return envValueAnswer(env, []string{"Enviroment_Temperation", "Enviroment_Temperature", "temperature", "Temperature", "temp", "Temp", "环境温度", "温度"}, "当前环境温度", "摄氏度"), true
	}
	if containsAny(question, "风速") {
		return envValueAnswer(env, []string{"Wind_Speed", "wind_speed", "windspeed", "风速"}, "当前风速", "米每秒"), true
	}
	if containsAny(question, "风向") {
		return envValueAnswer(env, []string{"Wind_Direction", "wind_direction", "wind_dir", "风向"}, "当前风向", "度"), true
	}
	if containsAny(question, "光照") {
		return envValueAnswer(env, []string{"Enviroment_Light", "light", "Light", "illumination", "Lux", "光照"}, "当前光照强度", "勒克斯"), true
	}
	return "当前环境数据：" + formatEnvData(env) + "。", true
}

func isEnvQuestion(text string) bool {
	if containsAny(text, "监测页面", "传感器数据", "在哪里查看", "怎么打开", "如何查看", "查看环境监测") {
		return false
	}
	return containsAny(text, "当前温度", "现在温度", "温度是多少", "温度多少", "温度呢", "当前湿度", "现在湿度", "湿度怎么样", "湿度多少", "湿度呢", "环境怎么样", "当前环境", "当前空气", "空气怎么样", "空气质量", "当前光照", "光照怎么样", "光照强度", "当前风速", "现在风速", "风速和风向", "风速是多少", "风速多少", "当前风向", "现在风向", "风向是多少", "风向呢", "pm2.5", "pm 2.5", "pm10", "pm 10")
}

func envValueAnswer(env map[string]interface{}, keys []string, label string, unit string) string {
	formatted, ok := findEnvValue(env, keys)
	if !ok {
		return "当前还没有收到" + label + "数据。"
	}
	return label + "是" + formatted + unit + "。"
}

func envCombinedAnswer(env map[string]interface{}) string {
	speed, hasSpeed := findEnvValue(env, []string{"Wind_Speed", "wind_speed", "windspeed", "风速"})
	direction, hasDirection := findEnvValue(env, []string{"Wind_Direction", "wind_direction", "wind_dir", "风向"})
	if hasSpeed && hasDirection {
		return "当前风速是" + speed + "米每秒，风向是" + direction + "度。"
	}
	if hasSpeed {
		return "当前风速是" + speed + "米每秒，暂未收到风向数据。"
	}
	if hasDirection {
		return "当前风向是" + direction + "度，暂未收到风速数据。"
	}
	return "当前还没有收到风速和风向数据。"
}

func (s *session) setEnvSnapshot(data map[string]interface{}) {
	if len(data) == 0 {
		return
	}
	cp := copyEnvMap(data)
	s.envMu.Lock()
	s.envSnapshot = cp
	s.envMu.Unlock()
	if s.envStore != nil {
		s.envStore.Set(cp)
	}
}

func (s *session) latestEnv() map[string]interface{} {
	s.envMu.RLock()
	if len(s.envSnapshot) > 0 {
		cp := copyEnvMap(s.envSnapshot)
		s.envMu.RUnlock()
		return cp
	}
	s.envMu.RUnlock()

	if global := s.envStore.Latest(); len(global) > 0 {
		return global
	}

	if s.envProvider == nil {
		return nil
	}
	return s.envProvider()
}

func findEnvValue(env map[string]interface{}, keys []string) (string, bool) {
	for _, key := range keys {
		if value, ok := env[key]; ok {
			formatted := strings.TrimSpace(formatEnvValue(value))
			if formatted != "" {
				return formatted, true
			}
		}
	}
	return "", false
}

func formatEnvData(env map[string]interface{}) string {
	keys := make([]string, 0, len(env))
	for key := range env {
		keys = append(keys, key)
	}
	sort.Strings(keys)

	parts := make([]string, 0, len(keys))
	for _, key := range keys {
		parts = append(parts, key+"="+formatEnvValue(env[key]))
	}
	return strings.Join(parts, "，")
}

func formatEnvValue(value interface{}) string {
	switch v := value.(type) {
	case string:
		return v
	case float64:
		return strings.TrimRight(strings.TrimRight(fmt.Sprintf("%.2f", v), "0"), ".")
	case float32:
		return strings.TrimRight(strings.TrimRight(fmt.Sprintf("%.2f", v), "0"), ".")
	case int:
		return fmt.Sprintf("%d", v)
	case int64:
		return fmt.Sprintf("%d", v)
	default:
		return fmt.Sprintf("%v", v)
	}
}

func (s *session) sendJSON(v event) error {
	s.writeMu.Lock()
	defer s.writeMu.Unlock()
	return s.conn.WriteJSON(v)
}

func (s *session) sendAudio(audio *tts.Audio) {
	payload := float32ToPCM16LE(audio.Samples, audio.SampleRate)
	s.writeMu.Lock()
	defer s.writeMu.Unlock()
	_ = s.conn.WriteMessage(websocket.BinaryMessage, payload)
}

func (s *session) close() {
	s.closeOnce.Do(func() {
		s.speakingID.Add(1)
		s.responding.Store(false)
		s.cancel()
		close(s.done)
		s.bgWg.Wait()
		s.recognizer.Close()
		s.synthesizer.Close()
		s.detector.Close()
		_ = s.conn.Close()
	})
}

type event struct {
	Type       string `json:"type"`
	State      string `json:"state,omitempty"`
	Text       string `json:"text,omitempty"`
	Message    string `json:"message,omitempty"`
	Command    string `json:"command,omitempty"`
	SampleRate int    `json:"sampleRate,omitempty"`
}

type controlMessage struct {
	Type string                 `json:"type"`
	Text string                 `json:"text"`
	Data map[string]interface{} `json:"data"`
}

type audioFrame struct {
	samples    []float32
	sampleRate int
	flags      uint32
}

func (f audioFrame) ttsPlaying() bool {
	return f.flags&audioFlagTTSPlaying != 0
}

func decodeAudioFrame(payload []byte) (audioFrame, error) {
	sampleRate := inputSampleRate
	var flags uint32
	if len(payload) >= 8 && string(payload[:4]) == "PCM1" {
		sampleRate = int(binary.LittleEndian.Uint32(payload[4:8]))
		payload = payload[8:]
	} else if len(payload) >= 12 && string(payload[:4]) == "PCM2" {
		sampleRate = int(binary.LittleEndian.Uint32(payload[4:8]))
		flags = binary.LittleEndian.Uint32(payload[8:12])
		payload = payload[12:]
	}
	if len(payload)%2 != 0 {
		return audioFrame{}, errors.New("PCM16 payload length must be even")
	}

	samples := make([]float32, len(payload)/2)
	for i := range samples {
		value := int16(binary.LittleEndian.Uint16(payload[i*2:]))
		samples[i] = float32(value) / 32768.0
	}
	return audioFrame{samples: samples, sampleRate: sampleRate, flags: flags}, nil
}

func float32ToPCM16LE(samples []float32, sampleRate int) []byte {
	payload := make([]byte, 8+len(samples)*2)
	copy(payload[:4], []byte("TTS1"))
	binary.LittleEndian.PutUint32(payload[4:8], uint32(sampleRate))
	for i, sample := range samples {
		sample = float32(math.Max(-1, math.Min(1, float64(sample))))
		binary.LittleEndian.PutUint16(payload[8+i*2:], uint16(int16(sample*32767)))
	}
	return payload
}

func normalizeSpeechText(text string) string {
	text = strings.TrimSpace(text)
	text = speechRolePattern.ReplaceAllString(text, "")
	text = speechMarkupPattern.ReplaceAllString(text, "")
	text = strings.ReplaceAll(text, "\r\n", "，")
	text = strings.ReplaceAll(text, "\n", "，")
	text = leadingListMarkerPattern.ReplaceAllString(text, "")
	text = normalizeShortPauses(text)
	text = normalizeDigitsForSpeech(text)
	return strings.TrimSpace(text)
}

func detectMapToolIntent(text string) (mapToolIntent, bool) {
	text = strings.TrimSpace(text)
	if text == "" {
		return mapToolIntent{}, false
	}

	if containsAny(text, "我现在在哪", "我在哪", "当前位置", "我的位置", "现在在什么位置", "现在位置", "这里是哪里", "设备位置", "终端位置") {
		return mapToolIntent{action: "current_location"}, true
	}

	if containsAny(text, "查看地图", "打开地图", "显示地图", "看地图") {
		return mapToolIntent{action: "show_map"}, true
	}

	if isAttributeQuestion(text) {
		return mapToolIntent{}, false
	}
	hasPlace := containsAny(text,
		"医院", "公交", "卫生间", "厕所", "政务", "办事", "社区", "公园", "银行", "餐厅", "吃饭", "肯德基")
	hasMapIntent := containsAny(text,
		"附近", "最近", "哪里", "在哪", "怎么走", "路线", "导航", "过去", "去", "地图")
	if hasPlace && hasMapIntent {
		return mapToolIntent{action: "route_to_poi"}, true
	}

	return mapToolIntent{}, false
}

func isAttributeQuestion(text string) bool {
	return containsAny(text, "电话", "密码", "开放时间", "几点", "预约", "菜单", "负责人", "编号", "审批状态", "时刻表", "联系方式")
}

func containsAny(text string, keywords ...string) bool {
	text = strings.ToLower(text)
	for _, keyword := range keywords {
		if strings.Contains(text, strings.ToLower(keyword)) {
			return true
		}
	}
	return false
}

func normalizeShortPauses(text string) string {
	replacer := strings.NewReplacer(
		"，", "、",
		",", "、",
		"；", "、",
		";", "、",
		"：", "、",
		":", "、",
	)
	return replacer.Replace(text)
}

func isSentenceEnd(runes []rune, i int) bool {
	if i < 0 || i >= len(runes) {
		return false
	}

	switch runes[i] {
	case '。', '！', '？', '!', '?', '\n':
		return true
	case '.':
		if i > 0 && isASCIIDigit(runes[i-1]) {
			return false
		}
		if i+1 < len(runes) && isASCIIDigit(runes[i+1]) {
			return false
		}
		return true
	default:
		return false
	}
}

func isASCIIDigit(r rune) bool {
	return r >= '0' && r <= '9'
}

func isSoftSpeechBoundary(r rune) bool {
	switch r {
	case '了', '呢', '吧', '吗', '嘛', '呀', '啊', '哦', '啦', '喽':
		return true
	default:
		return false
	}
}

func normalizeDigitsForSpeech(text string) string {
	var builder strings.Builder
	for _, r := range text {
		switch r {
		case '0':
			builder.WriteRune('零')
		case '1':
			builder.WriteRune('一')
		case '2':
			builder.WriteRune('二')
		case '3':
			builder.WriteRune('三')
		case '4':
			builder.WriteRune('四')
		case '5':
			builder.WriteRune('五')
		case '6':
			builder.WriteRune('六')
		case '7':
			builder.WriteRune('七')
		case '8':
			builder.WriteRune('八')
		case '9':
			builder.WriteRune('九')
		default:
			builder.WriteRune(r)
		}
	}
	return builder.String()
}

func splitSentences(text string) []string {
	var parts []string
	var current strings.Builder
	runes := []rune(text)
	for i, r := range runes {
		current.WriteRune(r)
		if isSentenceEnd(runes, i) {
			appendPart(&parts, current.String())
			current.Reset()
		}
	}
	appendPart(&parts, current.String())
	return parts
}

func appendPart(parts *[]string, text string) {
	text = strings.TrimSpace(text)
	if text != "" {
		*parts = append(*parts, text)
	}
}
