//go:build !cgo

// ASR 语音识别包存根，CGO 禁用时提供空实现
package asr

import "fmt"

// StreamingRecognizer 存根识别器，CGO 不可用时为空结构
type StreamingRecognizer struct{}

// NewStreamingRecognizer 返回错误提示，告知需要启用 CGO
func NewStreamingRecognizer(modelDir string) (*StreamingRecognizer, error) {
	return nil, fmt.Errorf("sherpa-onnx ASR requires CGO; enable CGO_ENABLED=1 and install a C compiler, model dir: %s", modelDir)
}

// AcceptAudio 存根方法，始终返回空字符串
func (r *StreamingRecognizer) AcceptAudio(samples []float32, inputSampleRate int) string {
	return ""
}

// IsEndpoint 存根方法，始终返回 false
func (r *StreamingRecognizer) IsEndpoint() bool {
	return false
}

// Reset 存根方法，不执行任何操作
func (r *StreamingRecognizer) Reset() {}

// Close 存根方法，不执行任何操作
func (r *StreamingRecognizer) Close() {}
