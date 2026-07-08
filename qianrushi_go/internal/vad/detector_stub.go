//go:build !cgo

// VAD 语音活动检测包存根，CGO 禁用时提供空实现
package vad

import "fmt"

// Detector 存根检测器，CGO 不可用时为空结构
type Detector struct{}

// NewDetector 返回错误提示，告知需要启用 CGO
func NewDetector(modelPath string) (*Detector, error) {
	return nil, fmt.Errorf("sherpa-onnx VAD requires CGO; enable CGO_ENABLED=1 and install a C compiler, model path: %s", modelPath)
}

// IsActive 存根方法，始终返回 false
func (d *Detector) IsActive(samples []float32, inputSampleRate int) bool {
	return false
}

// Close 存根方法，不执行任何操作
func (d *Detector) Close() {}
