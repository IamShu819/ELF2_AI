//go:build cgo

// TTS 语音合成包，基于 sherpa-onnx 提供文本转语音能力
package tts

import (
	"fmt"
	"log"
	"os"
	"path/filepath"
	"strconv"
	"strings"

	sherpa "github.com/k2-fsa/sherpa-onnx-go/sherpa_onnx"
)

// Audio 表示合成的音频数据，包含采样率和 PCM 样本
type Audio struct {
	SampleRate int
	Samples    []float32
}

// Synthesizer 文本转语音合成器
type Synthesizer struct {
	tts   *sherpa.OfflineTts
	sid   int
	speed float32
}

// ChunkCallback receives generated PCM samples while TTS is still running.
// Returning false asks sherpa-onnx to stop the current generation.
type ChunkCallback func(audio *Audio) bool

// NewSynthesizer 创建语音合成器，modelDir 为模型文件目录
func NewSynthesizer(modelDir string) (*Synthesizer, error) {
	if isKokoroModelDir(modelDir) {
		return newKokoroSynthesizer(modelDir)
	}
	return newVitsSynthesizer(modelDir)
}

func newKokoroSynthesizer(modelDir string) (*Synthesizer, error) {
	model := filepath.Join(modelDir, "model.onnx")
	voices := filepath.Join(modelDir, "voices.bin")
	tokens := filepath.Join(modelDir, "tokens.txt")
	lexicon := filepath.Join(modelDir, "lexicon-zh.txt")
	dataDir := filepath.Join(modelDir, "espeak-ng-data")

	for _, path := range []string{model, voices, tokens, lexicon, dataDir} {
		if _, err := os.Stat(path); err != nil {
			return nil, fmt.Errorf("Kokoro TTS model file not found: %s", path)
		}
	}

	config := &sherpa.OfflineTtsConfig{
		Model: sherpa.OfflineTtsModelConfig{
			Kokoro: sherpa.OfflineTtsKokoroModelConfig{
				Model:       model,
				Voices:      voices,
				Tokens:      tokens,
				DataDir:     dataDir,
				Lexicon:     lexicon,
				Lang:        "zh",
				LengthScale: envFloat32("VOICE_TTS_LENGTH_SCALE", 0.9),
			},
			NumThreads: envInt("VOICE_TTS_THREADS", 4),
			Provider:   "cpu",
		},
		RuleFsts:        existingCSV(filepath.Join(modelDir, "phone-zh.fst"), filepath.Join(modelDir, "date-zh.fst"), filepath.Join(modelDir, "number-zh.fst")),
		MaxNumSentences: 1,
		SilenceScale:    envFloat32("VOICE_TTS_SILENCE_SCALE", 0.02),
	}

	engine := sherpa.NewOfflineTts(config)
	if engine == nil {
		return nil, fmt.Errorf("failed to create sherpa Kokoro offline TTS")
	}

	sid := envInt("VOICE_TTS_SID", 46)
	speed := envFloat32("VOICE_TTS_SPEED", 1.0)
	log.Printf("TTS engine initialized, engine=kokoro, model=%s, sid=%d, speed=%.2f", model, sid, speed)
	return &Synthesizer{tts: engine, sid: sid, speed: speed}, nil
}

func newVitsSynthesizer(modelDir string) (*Synthesizer, error) {
	model := firstExisting(
		filepath.Join(modelDir, "model.onnx"),
		filepath.Join(modelDir, "zh_CN-huayan-medium.onnx"),
	)
	tokens := filepath.Join(modelDir, "tokens.txt")
	lexicon := filepath.Join(modelDir, "lexicon.txt")

	// 检查模型文件是否存在
	for _, path := range []string{model, tokens, lexicon} {
		if path == "" {
			return nil, fmt.Errorf("TTS model file not found in: %s", modelDir)
		}
		if _, err := os.Stat(path); err != nil {
			return nil, fmt.Errorf("TTS model file not found: %s", path)
		}
	}

	config := &sherpa.OfflineTtsConfig{
		Model: sherpa.OfflineTtsModelConfig{
			Vits: sherpa.OfflineTtsVitsModelConfig{
				Model:       model,
				Tokens:      tokens,
				Lexicon:     lexicon,
				DictDir:     filepath.Join(modelDir, "dict"),
				NoiseScale:  0.667,
				NoiseScaleW: 0.8,
				LengthScale: 0.9,
			},
			NumThreads: 4,
			Provider:   "cpu",
		},
		MaxNumSentences: 1,
		SilenceScale:    0.1,
	}

	engine := sherpa.NewOfflineTts(config)
	if engine == nil {
		return nil, fmt.Errorf("failed to create sherpa offline TTS")
	}

	speed := envFloat32("VOICE_TTS_SPEED", 1.0)
	log.Printf("TTS engine initialized, engine=vits, model=%s, sid=0, speed=%.2f", model, speed)
	return &Synthesizer{tts: engine, sid: 0, speed: speed}, nil
}

// Synthesize 将文本合成为音频，返回音频数据
func (s *Synthesizer) Synthesize(text string) (*Audio, error) {
	text = strings.TrimSpace(text)
	if text == "" {
		return nil, fmt.Errorf("empty text")
	}

	audio := s.tts.Generate(text, s.sid, s.speed)
	if audio == nil || len(audio.Samples) == 0 {
		return nil, fmt.Errorf("TTS generated no audio")
	}

	return &Audio{SampleRate: audio.SampleRate, Samples: audio.Samples}, nil
}

// SynthesizeStream streams generated audio chunks through onChunk as soon as
// sherpa-onnx produces them. It still returns only after the current sentence
// generation finishes or the callback asks it to stop.
func (s *Synthesizer) SynthesizeStream(text string, onChunk ChunkCallback) error {
	text = strings.TrimSpace(text)
	if text == "" {
		return fmt.Errorf("empty text")
	}
	if onChunk == nil {
		return fmt.Errorf("nil TTS chunk callback")
	}

	sampleRate := s.tts.SampleRate()
	chunkCount := 0
	stopped := false
	audio := s.tts.GenerateWithCallback(text, s.sid, s.speed, func(samples []float32) bool {
		if len(samples) == 0 {
			return true
		}
		chunkCount++
		keepGoing := onChunk(&Audio{
			SampleRate: sampleRate,
			Samples:    samples,
		})
		if !keepGoing {
			stopped = true
		}
		return keepGoing
	})

	if audio == nil {
		return finishSynthesizeStreamFallback(nil, chunkCount, stopped, onChunk)
	}

	return finishSynthesizeStreamFallback(&Audio{SampleRate: audio.SampleRate, Samples: audio.Samples}, chunkCount, stopped, onChunk)
}

// Close 释放 TTS 引擎资源
func (s *Synthesizer) Close() {
	if s.tts != nil {
		sherpa.DeleteOfflineTts(s.tts)
		s.tts = nil
	}
}

// firstExisting 返回路径列表中第一个存在的文件路径
func firstExisting(paths ...string) string {
	for _, path := range paths {
		if _, err := os.Stat(path); err == nil {
			return path
		}
	}
	if len(paths) > 0 {
		return paths[0]
	}
	return ""
}

func isKokoroModelDir(modelDir string) bool {
	_, err := os.Stat(filepath.Join(modelDir, "voices.bin"))
	return err == nil
}

func existingCSV(paths ...string) string {
	var existing []string
	for _, path := range paths {
		if _, err := os.Stat(path); err == nil {
			existing = append(existing, path)
		}
	}
	return strings.Join(existing, ",")
}

func envInt(key string, fallback int) int {
	value := strings.TrimSpace(os.Getenv(key))
	if value == "" {
		return fallback
	}
	parsed, err := strconv.Atoi(value)
	if err != nil || parsed <= 0 {
		return fallback
	}
	return parsed
}

func envFloat32(key string, fallback float32) float32 {
	value := strings.TrimSpace(os.Getenv(key))
	if value == "" {
		return fallback
	}
	parsed, err := strconv.ParseFloat(value, 32)
	if err != nil || parsed <= 0 {
		return fallback
	}
	return float32(parsed)
}
