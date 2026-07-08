package tts

import (
	"bytes"
	"log"
	"reflect"
	"strings"
	"testing"
)

func TestFinishSynthesizeStreamFallbackSkipsWhenCallbackProducedAudio(t *testing.T) {
	called := false
	err := finishSynthesizeStreamFallback(&Audio{SampleRate: 16000, Samples: []float32{1, 2, 3}}, 1, false, func(*Audio) bool {
		called = true
		return true
	})
	if err != nil {
		t.Fatalf("finishSynthesizeStreamFallback returned error: %v", err)
	}
	if called {
		t.Fatal("fallback called onChunk after callback already produced valid audio")
	}
}

func TestStreamFallbackAudioLogsOnlyWhenFallbackEnabled(t *testing.T) {
	var buf bytes.Buffer
	originalWriter := log.Writer()
	originalFlags := log.Flags()
	log.SetOutput(&buf)
	log.SetFlags(0)
	defer func() {
		log.SetOutput(originalWriter)
		log.SetFlags(originalFlags)
	}()

	called := false
	if err := finishSynthesizeStreamFallback(&Audio{SampleRate: 16000, Samples: []float32{1, 2, 3}}, 1, false, func(*Audio) bool {
		called = true
		return true
	}); err != nil {
		t.Fatalf("finishSynthesizeStreamFallback returned error: %v", err)
	}
	if called {
		t.Fatal("fallback should not call onChunk when callback already streamed audio")
	}
	if buf.Len() != 0 {
		t.Fatalf("callback streaming path wrote fallback log: %q", buf.String())
	}

	if err := finishSynthesizeStreamFallback(&Audio{SampleRate: 10, Samples: []float32{0, 1, 2, 3, 4}}, 0, false, func(*Audio) bool { return true }); err != nil {
		t.Fatalf("finishSynthesizeStreamFallback returned error: %v", err)
	}
	got := buf.String()
	for _, want := range []string{"TTS full-audio fallback enabled", "sample_rate=10", "total_samples=5", "chunk_ms=200", "chunks=3"} {
		if !strings.Contains(got, want) {
			t.Fatalf("fallback log %q missing %q", got, want)
		}
	}
}

func TestStreamFallbackAudioPreservesSamplesAndRemainder(t *testing.T) {
	const sampleRate = 10
	samples := []float32{0, 1, 2, 3, 4}
	var chunks [][]float32
	err := streamFallbackAudio(&Audio{SampleRate: sampleRate, Samples: samples}, func(audio *Audio) bool {
		if audio.SampleRate != sampleRate {
			t.Fatalf("chunk sample rate=%d, want %d", audio.SampleRate, sampleRate)
		}
		chunks = append(chunks, append([]float32(nil), audio.Samples...))
		return true
	})
	if err != nil {
		t.Fatalf("streamFallbackAudio returned error: %v", err)
	}
	wantChunks := [][]float32{{0, 1}, {2, 3}, {4}}
	if !reflect.DeepEqual(chunks, wantChunks) {
		t.Fatalf("chunks=%v, want %v", chunks, wantChunks)
	}
	var joined []float32
	for _, chunk := range chunks {
		joined = append(joined, chunk...)
	}
	if !reflect.DeepEqual(joined, samples) {
		t.Fatalf("joined samples=%v, want %v", joined, samples)
	}
}

func TestStreamFallbackAudioStopsWhenOnChunkReturnsFalse(t *testing.T) {
	const sampleRate = 10
	var chunks [][]float32
	err := streamFallbackAudio(&Audio{SampleRate: sampleRate, Samples: []float32{0, 1, 2, 3, 4}}, func(audio *Audio) bool {
		chunks = append(chunks, append([]float32(nil), audio.Samples...))
		return len(chunks) < 2
	})
	if err != nil {
		t.Fatalf("streamFallbackAudio returned error: %v", err)
	}
	want := [][]float32{{0, 1}, {2, 3}}
	if !reflect.DeepEqual(chunks, want) {
		t.Fatalf("chunks=%v, want stopped after second chunk %v", chunks, want)
	}
}

func TestStreamFallbackAudioEmptyAudioReturnsClearError(t *testing.T) {
	for _, audio := range []*Audio{nil, &Audio{SampleRate: 16000}} {
		err := streamFallbackAudio(audio, func(*Audio) bool { return true })
		if err == nil || !strings.Contains(err.Error(), "TTS generated no audio") {
			t.Fatalf("err=%v, want clear no audio error", err)
		}
	}
}

func TestStreamFallbackAudioInvalidCallbackAndSampleRate(t *testing.T) {
	if err := streamFallbackAudio(&Audio{SampleRate: 16000, Samples: []float32{1}}, nil); err == nil || !strings.Contains(err.Error(), "nil TTS chunk callback") {
		t.Fatalf("nil callback err=%v", err)
	}
	err := streamFallbackAudio(&Audio{SampleRate: 0, Samples: []float32{1}}, func(*Audio) bool { return true })
	if err == nil || !strings.Contains(err.Error(), "invalid sample rate") {
		t.Fatalf("invalid sample rate err=%v", err)
	}
}
