//go:build cgo

// ASR 语音识别包，基于 sherpa-onnx 提供流式语音识别能力
package asr

import (
	"fmt"
	"os"
	"path/filepath"
	"sync"

	sherpa "github.com/k2-fsa/sherpa-onnx-go/sherpa_onnx"
)

// 音频采样率，16kHz
const sampleRate = 16000

// StreamingRecognizer 流式语音识别器，支持实时音频输入和文本输出
type StreamingRecognizer struct {
	mu         sync.Mutex
	recognizer *sherpa.OnlineRecognizer
	stream     *sherpa.OnlineStream
}

// NewStreamingRecognizer 创建流式语音识别器，modelDir 为模型文件目录
func NewStreamingRecognizer(modelDir string) (*StreamingRecognizer, error) {
	encoder := filepath.Join(modelDir, "encoder.int8.onnx")
	decoder := filepath.Join(modelDir, "decoder.int8.onnx")
	tokens := filepath.Join(modelDir, "tokens.txt")

	// 检查模型文件是否存在
	for _, path := range []string{encoder, decoder, tokens} {
		if _, err := os.Stat(path); err != nil {
			return nil, fmt.Errorf("ASR model file not found: %s", path)
		}
	}

	config := &sherpa.OnlineRecognizerConfig{
		FeatConfig: sherpa.FeatureConfig{
			SampleRate: sampleRate,
			FeatureDim: 80,
		},
		ModelConfig: sherpa.OnlineModelConfig{
			Paraformer: sherpa.OnlineParaformerModelConfig{
				Encoder: encoder,
				Decoder: decoder,
			},
			Tokens:     tokens,
			NumThreads: 2,
			Provider:   "cpu",
		},
		DecodingMethod:          "greedy_search",
		EnableEndpoint:          1,
		Rule1MinTrailingSilence: 2.4,
		Rule2MinTrailingSilence: 1.2,
		Rule3MinUtteranceLength: 20,
	}

	recognizer := sherpa.NewOnlineRecognizer(config)
	if recognizer == nil {
		return nil, fmt.Errorf("failed to create sherpa online recognizer")
	}

	stream := sherpa.NewOnlineStream(recognizer)
	if stream == nil {
		sherpa.DeleteOnlineRecognizer(recognizer)
		return nil, fmt.Errorf("failed to create sherpa online stream")
	}

	return &StreamingRecognizer{recognizer: recognizer, stream: stream}, nil
}

// AcceptAudio 输入音频数据并返回识别文本
func (r *StreamingRecognizer) AcceptAudio(samples []float32, inputSampleRate int) string {
	r.mu.Lock()
	defer r.mu.Unlock()

	if len(samples) == 0 {
		return ""
	}
	if inputSampleRate <= 0 {
		inputSampleRate = sampleRate
	}
	if inputSampleRate != sampleRate {
		samples = resampleLinear(samples, inputSampleRate, sampleRate)
		inputSampleRate = sampleRate
	}

	// 送入音频波形并执行解码
	r.stream.AcceptWaveform(inputSampleRate, samples)
	for r.recognizer.IsReady(r.stream) {
		r.recognizer.Decode(r.stream)
	}

	result := r.recognizer.GetResult(r.stream)
	if result == nil {
		return ""
	}
	return result.Text
}

func resampleLinear(samples []float32, fromRate, toRate int) []float32 {
	if len(samples) == 0 || fromRate <= 0 || toRate <= 0 || fromRate == toRate {
		return samples
	}
	outLen := int(float64(len(samples)) * float64(toRate) / float64(fromRate))
	if outLen <= 0 {
		return nil
	}
	out := make([]float32, outLen)
	scale := float64(fromRate) / float64(toRate)
	for i := range out {
		pos := float64(i) * scale
		j := int(pos)
		if j >= len(samples)-1 {
			out[i] = samples[len(samples)-1]
			continue
		}
		frac := float32(pos - float64(j))
		out[i] = samples[j]*(1-frac) + samples[j+1]*frac
	}
	return out
}

// IsEndpoint 判断当前语音是否到达端点（结束）
func (r *StreamingRecognizer) IsEndpoint() bool {
	r.mu.Lock()
	defer r.mu.Unlock()
	return r.recognizer.IsEndpoint(r.stream)
}

// Reset 重置识别器状态，清空当前流
func (r *StreamingRecognizer) Reset() {
	r.mu.Lock()
	defer r.mu.Unlock()
	r.recognizer.Reset(r.stream)
}

// Close 释放识别器和流资源
func (r *StreamingRecognizer) Close() {
	r.mu.Lock()
	defer r.mu.Unlock()
	if r.stream != nil {
		sherpa.DeleteOnlineStream(r.stream)
		r.stream = nil
	}
	if r.recognizer != nil {
		sherpa.DeleteOnlineRecognizer(r.recognizer)
		r.recognizer = nil
	}
}
