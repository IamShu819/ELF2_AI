//go:build !cgo

// TTS 语音合成包存根，CGO 禁用时提供空实现
package tts

import "fmt"

// Audio 存根音频数据，与 CGO 版本结构一致
type Audio struct {
	SampleRate int
	Samples    []float32
}

// Synthesizer 存根合成器，CGO 不可用时为空结构
type Synthesizer struct{}

// ChunkCallback receives generated PCM samples while TTS is still running.
type ChunkCallback func(audio *Audio) bool

// NewSynthesizer 返回错误提示，告知需要启用 CGO
func NewSynthesizer(modelDir string) (*Synthesizer, error) {
	return nil, fmt.Errorf("sherpa-onnx TTS requires CGO; enable CGO_ENABLED=1 and install a C compiler, model dir: %s", modelDir)
}

// Synthesize 存根方法，始终返回错误
func (s *Synthesizer) Synthesize(text string) (*Audio, error) {
	return nil, fmt.Errorf("sherpa-onnx TTS requires CGO")
}

// SynthesizeStream 存根方法，始终返回错误
func (s *Synthesizer) SynthesizeStream(text string, onChunk ChunkCallback) error {
	return fmt.Errorf("sherpa-onnx TTS requires CGO")
}

// Close 存根方法，不执行任何操作
func (s *Synthesizer) Close() {}
