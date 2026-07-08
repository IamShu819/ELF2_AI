// Voice engine backend entry point.
//
// Exposes /voice and /env WebSocket endpoints, integrates ASR/TTS/VAD,
// LLM/RAG, MQTT, and optional direct STM32 serial reading.

package main

import (
	"context"
	"flag"
	"fmt"
	"log"
	"net/http"
	"os"
	"path/filepath"
	"strings"
	"time"

	"comm-gateway/internal/alert"
	"comm-gateway/internal/asr"
	"comm-gateway/internal/envws"
	"comm-gateway/internal/llm"
	mqttclient "comm-gateway/internal/mqtt"
	"comm-gateway/internal/rag"
	"comm-gateway/internal/serial"
	"comm-gateway/internal/tts"
	"comm-gateway/internal/vad"
	voicews "comm-gateway/internal/websocket"
)

func main() {
	// Default local model root for legacy Windows deployments.
	defaultModelRoot := `D:\iot\iotcloubs\hubeijinengdasai\shuzi\houduan-D435`

	// Command-line flags / environment variables.
	addr := flag.String("addr", envString("VOICE_ADDR", ":8080"), "HTTP listen address")
	asrDir := flag.String("asr-dir", envString("VOICE_ASR_DIR",
		filepath.Join("models", "asr", "sherpa-onnx-streaming-paraformer-bilingual-zh-en")),
		"sherpa-onnx streaming ASR model directory")
	ttsDir := flag.String("tts-dir", envString("VOICE_TTS_DIR",
		defaultTTSDir()),
		"sherpa-onnx TTS model directory")
	vadModel := flag.String("vad-model", envString("VOICE_VAD_MODEL",
		filepath.Join("models", "vad", "silero_vad.onnx")),
		"sherpa-onnx Silero VAD model path")
	llmURL := flag.String("llm-url", envString("VOICE_LLM_URL",
		"http://192.168.31.12:8000/v1/chat/completions"),
		"OpenAI-compatible chat completions URL")
	llmModel := flag.String("llm-model", envString("VOICE_LLM_MODEL",
		"qwen-2.5-7b-instruct"),
		"LLM model name")
	llmBackend := flag.String("llm-backend", envString("VOICE_LLM_BACKEND", "http"),
		"LLM backend type: http, rkllm-daemon, or rkllm-cli")
	rkllmBin := flag.String("rkllm-bin", envString("VOICE_RKLLM_BIN", "/home/elf/AI/llm_demo"),
		"RKLLM CLI executable path")
	rkllmModel := flag.String("rkllm-model", envString("VOICE_RKLLM_MODEL", "/home/elf/AI/DeepSeek-R1-Distill-Qwen-1.5B_W8A8_RK3588.rkllm"),
		"RKLLM model file path")
	rkllmWorkDir := flag.String("rkllm-workdir", envString("VOICE_RKLLM_WORKDIR", ""),
		"RKLLM CLI working directory")
	rkllmMaxNewTokens := flag.Int("rkllm-max-new-tokens", envInt("VOICE_RKLLM_MAX_NEW_TOKENS", 10000),
		"RKLLM max new tokens")
	rkllmMaxContext := flag.Int("rkllm-max-context", envInt("VOICE_RKLLM_MAX_CONTEXT", 10000),
		"RKLLM max context tokens")
	rkllmTimeoutSec := flag.Int("rkllm-timeout-sec", envInt("VOICE_RKLLM_TIMEOUT_SEC", 120),
		"RKLLM request timeout in seconds")
	modelRoot := flag.String("model-root", envString("VOICE_MODEL_ROOT", defaultModelRoot),
		"local model backend root")

	// Serial / MQTT flags.
	serialPort := flag.String("serial-port", envString("VOICE_SERIAL_PORT", "/dev/ttySTM32"),
		"STM32 serial port")
	serialBaud := flag.Int("serial-baud", envInt("VOICE_SERIAL_BAUD", 115200),
		"serial baud rate")
	serialReaderEnabled := flag.Bool("serial-reader-enabled", envBool("VOICE_SERIAL_READER_ENABLED", true),
		"enable direct STM32 serial reading in Go")
	mqttBroker := flag.String("mqtt-broker", envString("VOICE_MQTT_BROKER", "192.168.31.68"),
		"MQTT broker host")
	mqttPort := flag.Int("mqtt-port", envInt("VOICE_MQTT_PORT", 1883),
		"MQTT broker port")
	mqttUser := flag.String("mqtt-user", envString("VOICE_MQTT_USER", ""),
		"MQTT username")
	mqttPass := flag.String("mqtt-pass", envString("VOICE_MQTT_PASS", ""),
		"MQTT password")

	flag.Parse()

	log.Printf("[startup] voice engine starting, model_root=%s", *modelRoot)
	log.Printf("[config] serial=%s baud=%d reader_enabled=%t", *serialPort, *serialBaud, *serialReaderEnabled)
	log.Printf("[config] mqtt=tcp://%s:%d", *mqttBroker, *mqttPort)
	log.Printf("[config] llm-backend=%s llm-url=%s model=%s", *llmBackend, *llmURL, *llmModel)
	if strings.HasPrefix(strings.ToLower(strings.TrimSpace(*llmBackend)), "rkllm") {
		log.Printf("[config] rkllm-bin=%s rkllm-model=%s", *rkllmBin, *rkllmModel)
	}

	// Engine factories.
	recognizerFactory := func() (*asr.StreamingRecognizer, error) {
		return asr.NewStreamingRecognizer(*asrDir)
	}
	synthesizerFactory := func() (*tts.Synthesizer, error) {
		return tts.NewSynthesizer(*ttsDir)
	}
	vadFactory := func() (*vad.Detector, error) {
		return vad.NewDetector(*vadModel)
	}
	dialogueLLM := newLLMBackend(*llmBackend, *llmURL, *llmModel, *rkllmBin, *rkllmModel, *rkllmWorkDir,
		*rkllmMaxNewTokens, *rkllmMaxContext, *rkllmTimeoutSec)
	warmupLLM(dialogueLLM, time.Duration(*rkllmTimeoutSec)*time.Second)

	ragConfig := rag.ConfigFromEnv(os.Getenv)
	for _, warning := range ragConfig.Warnings {
		log.Printf("[rag] config warning: %s", warning)
	}
	ragRuntime := rag.NewRuntime(context.Background(), ragConfig)
	if ragRuntime.Enabled() {
		log.Printf("[rag] enabled knowledge_dir=%s top_k=%d", ragConfig.KnowledgeDir, ragConfig.Retrieval.TopK)
	} else if err := ragRuntime.LoadError(); err != nil {
		log.Printf("[rag] disabled: %v", err)
	} else {
		log.Printf("[rag] disabled by config")
	}

	// Optional direct STM32 serial reader.
	serialReader := serial.NewReader(serial.Config{
		Port: *serialPort,
		Baud: *serialBaud,
	})

	// Process-wide environment snapshot shared by voice sessions.
	envStore := voicews.NewEnvStore()

	// MQTT client.
	mqttCli := mqttclient.NewClient(mqttclient.Config{
		Broker:   *mqttBroker,
		Port:     *mqttPort,
		Username: *mqttUser,
		Password: *mqttPass,
	})

	// Voice WebSocket handler.
	voiceHandler := voicews.NewHandler(voicews.Config{
		RecognizerFactory:  recognizerFactory,
		SynthesizerFactory: synthesizerFactory,
		VADFactory:         vadFactory,
		LLM:                dialogueLLM,
		EnvProvider:        envStore.Latest,
		EnvStore:           envStore,
		RAG:                ragRuntime,
	})

	if *serialReaderEnabled {
		serialReader.Start()
		log.Printf("[serial] reader started on %s @ %d baud", *serialPort, *serialBaud)
	} else {
		log.Printf("[serial] reader disabled by VOICE_SERIAL_READER_ENABLED=false; expecting Qt /env serial_json")
	}

	// Alarm manager.
	alertMgr := alert.NewManager(synthesizerFactory)

	// Environment WebSocket handler.
	envHandler := envws.NewHandler(serialReader, alertMgr)
	envHandler.SetRawPublisher(mqttCli.PublishRaw)
	envHandler.SetWarmPublisher(mqttCli.PublishWarm)
	envHandler.SetEnvSnapshotSink(envStore.Set)
	envHandler.SetEnvSnapshotProvider(envStore.Latest)

	// MQTT callbacks for warm events and STM32 commands.
	mqttCli.OnWarm(func(code string) {
		alertMgr.HandleWarm(code)
	})
	mqttCli.OnCmdPayload(func(payload []byte) {
		command := string(payload)
		if strings.TrimSpace(command) == "" {
			log.Printf("[mqtt] empty stm32 command ignored")
			return
		}
		if n := envHandler.BroadcastSTM32Command(command); n == 0 {
			log.Printf("[mqtt] massge command received but no env client online")
			if vn := voiceHandler.BroadcastSTM32Command(command); vn > 0 {
				log.Printf("[mqtt] massge command forwarded through voice fallback to %d client(s)", vn)
			}
		}
	})

	// Start MQTT in background-reconnect mode.
	if err := mqttCli.Start(); err != nil {
		log.Printf("[mqtt] startup error: %v (continuing without MQTT)", err)
	} else if mqttCli.IsConnected() {
		log.Printf("[mqtt] connected to %s:%d", *mqttBroker, *mqttPort)
	} else {
		log.Printf("[mqtt] background reconnect enabled for %s:%d", *mqttBroker, *mqttPort)
	}

	// Broadcast alarm events to /env clients.
	alertMgr.OnAlarm(func(ev alert.AlarmEvent) {
		envHandler.BroadcastAlarm(ev)
	})

	// Direct serial reader fan-out. Disabled on LubanCat remote-LLM deployment.
	if *serialReaderEnabled {
		go func() {
			for data := range serialReader.DataChan() {
				envStore.Set(data)
				envHandler.PushData(data)
			}
		}()
	}

	/* ---- HTTP 璺敱娉ㄥ唽 ---- */
	mux := http.NewServeMux()
	mux.Handle("/voice", voiceHandler)
	mux.Handle("/env", envHandler)
	mux.HandleFunc("/healthz", func(w http.ResponseWriter, _ *http.Request) {
		_, _ = w.Write([]byte("ok"))
	})

	log.Printf("[startup] voice engine ready, Qt WebSocket endpoints: ws://127.0.0.1%s/voice ws://127.0.0.1%s/env", *addr, *addr)
	if err := http.ListenAndServe(*addr, mux); err != nil {
		log.Fatal(err)
	}
}

func envString(key, fallback string) string {
	if value := os.Getenv(key); value != "" {
		return value
	}
	return fallback
}

func envInt(key string, fallback int) int {
	if value := os.Getenv(key); value != "" {
		var v int
		if _, err := fmt.Sscanf(value, "%d", &v); err == nil {
			return v
		}
	}
	return fallback
}

type warmupStreamer interface {
	Warmup(ctx context.Context) error
}

func warmupLLM(streamer llm.Streamer, timeout time.Duration) {
	warmup, ok := streamer.(warmupStreamer)
	if !ok {
		return
	}
	ctx, cancel := context.WithTimeout(context.Background(), timeout)
	defer cancel()
	start := time.Now()
	log.Printf("[llm] warming up persistent model")
	if err := warmup.Warmup(ctx); err != nil {
		log.Printf("[llm] warmup failed: %v (will retry on first request)", err)
		return
	}
	log.Printf("[llm] warmup finished in %s", time.Since(start).Round(time.Millisecond))
}

func newLLMBackend(backend string, llmURL string, llmModel string, rkllmBin string, rkllmModel string, rkllmWorkDir string,
	rkllmMaxNewTokens int, rkllmMaxContext int, rkllmTimeoutSec int) llm.Streamer {
	switch strings.ToLower(strings.TrimSpace(backend)) {
	case "rkllm", "rkllm-daemon", "rkllm-service", "daemon", "persistent":
		return llm.NewRKLLMDaemonClient(llm.RKLLMCLIConfig{
			BinaryPath:      rkllmBin,
			ModelPath:       rkllmModel,
			WorkDir:         rkllmWorkDir,
			MaxNewTokens:    rkllmMaxNewTokens,
			MaxContext:      rkllmMaxContext,
			Timeout:         time.Duration(rkllmTimeoutSec) * time.Second,
			UsePromptPrefix: !envBool("VOICE_RKLLM_RAW_PROMPT", false),
			StripThink:      !envBool("VOICE_RKLLM_KEEP_THINK", false),
		})
	case "rkllm-cli", "cli":
		return llm.NewRKLLMCLIClient(llm.RKLLMCLIConfig{
			BinaryPath:      rkllmBin,
			ModelPath:       rkllmModel,
			WorkDir:         rkllmWorkDir,
			MaxNewTokens:    rkllmMaxNewTokens,
			MaxContext:      rkllmMaxContext,
			Timeout:         time.Duration(rkllmTimeoutSec) * time.Second,
			UsePromptPrefix: !envBool("VOICE_RKLLM_RAW_PROMPT", false),
			StripThink:      !envBool("VOICE_RKLLM_KEEP_THINK", false),
		})
	default:
		return llm.NewOpenAIClient(llm.Config{
			BaseURL:     llmURL,
			Model:       llmModel,
			Temperature: 0.3,
			MaxTokens:   256,
		})
	}
}

func envBool(key string, fallback bool) bool {
	raw := strings.TrimSpace(os.Getenv(key))
	value := strings.ToLower(raw)
	if value == "" {
		return fallback
	}
	switch value {
	case "1", "true", "t", "yes", "y", "on", "enabled", "enable":
		return true
	case "0", "false", "f", "no", "n", "off", "disabled", "disable":
		return false
	default:
		log.Printf("[config] invalid boolean %s=%q, using default %t", key, raw, fallback)
		return fallback
	}
}

func defaultTTSDir() string {
	kokoroDir := filepath.Join("models", "tts", "kokoro-multi-lang-v1_0")
	if os.Getenv("VOICE_TTS_ENGINE") == "kokoro" {
		return kokoroDir
	}
	return filepath.Join("models", "tts", "vits-melo-tts-zh_en")
}
