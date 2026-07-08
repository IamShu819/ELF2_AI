//go:build cgo

// VAD 语音活动检测包，基于 sherpa-onnx 提供语音端点检测能力
package vad

import (
	"fmt"
	"math"
	"os"

	sherpa "github.com/k2-fsa/sherpa-onnx-go/sherpa_onnx"
)

// 音频采样率，16kHz
const sampleRate = 16000

// Detector 语音活动检测器，判断音频片段中是否包含人声
type Detector struct {
	vad *sherpa.VoiceActivityDetector
}

// NewDetector 创建语音活动检测器，modelPath 为 Silero VAD 模型文件路径
func NewDetector(modelPath string) (*Detector, error) {
	if _, err := os.Stat(modelPath); err != nil {
		return nil, fmt.Errorf("VAD model file not found: %s", modelPath)
	}

	config := &sherpa.VadModelConfig{
		SileroVad: sherpa.SileroVadModelConfig{
			Model:              modelPath,
			Threshold:          0.5,
			MinSilenceDuration: 0.35,
			MinSpeechDuration:  0.15,
			WindowSize:         512,
			MaxSpeechDuration:  20,
		},
		SampleRate: sampleRate,
		NumThreads: 1,
		Provider:   "cpu",
	}

	engine := sherpa.NewVoiceActivityDetector(config, 30)
	if engine == nil {
		return nil, fmt.Errorf("failed to create sherpa VAD")
	}

	return &Detector{vad: engine}, nil
}

// IsActive 判断音频样本中是否包含活跃语音
func (d *Detector) IsActive(samples []float32, inputSampleRate int) bool {
	if len(samples) == 0 {
		return false
	}
	if inputSampleRate > 0 && inputSampleRate != sampleRate {
		samples = resampleLinear(samples, inputSampleRate, sampleRate)
	}

	// 先使用 VAD 模型检测
	d.vad.AcceptWaveform(samples)
	if d.vad.IsSpeech() {
		return true
	}

	// VAD 未检出时，用能量阈值作为补充判断
	return hasEnoughEnergy(samples)
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

// Close 释放 VAD 检测器资源
func (d *Detector) Close() {
	if d.vad != nil {
		sherpa.DeleteVoiceActivityDetector(d.vad)
		d.vad = nil
	}
}

// hasEnoughEnergy 通过 RMS 能量阈值判断音频是否包含有效信号
func hasEnoughEnergy(samples []float32) bool {
	var sum float64
	for _, sample := range samples {
		sum += float64(sample * sample)
	}
	rms := math.Sqrt(sum / float64(len(samples)))
	return rms > 0.012
}
