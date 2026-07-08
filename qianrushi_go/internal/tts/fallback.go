package tts

import (
	"fmt"
	"log"
)

const fallbackChunkMillis = 200

func finishSynthesizeStreamFallback(audio *Audio, validChunkCount int, stopped bool, onChunk ChunkCallback) error {
	if stopped || validChunkCount > 0 {
		return nil
	}
	return streamFallbackAudio(audio, onChunk)
}

func streamFallbackAudio(audio *Audio, onChunk ChunkCallback) error {
	if onChunk == nil {
		return fmt.Errorf("nil TTS chunk callback")
	}
	if audio == nil || len(audio.Samples) == 0 {
		return fmt.Errorf("TTS generated no audio")
	}
	if audio.SampleRate <= 0 {
		return fmt.Errorf("TTS generated audio with invalid sample rate %d", audio.SampleRate)
	}

	samplesPerChunk := fallbackSamplesPerChunk(audio.SampleRate)
	chunkCount := fallbackChunkCount(len(audio.Samples), samplesPerChunk)
	log.Printf("TTS full-audio fallback enabled: sample_rate=%d total_samples=%d chunk_ms=%d chunks=%d", audio.SampleRate, len(audio.Samples), fallbackChunkMillis, chunkCount)

	for start := 0; start < len(audio.Samples); start += samplesPerChunk {
		end := start + samplesPerChunk
		if end > len(audio.Samples) {
			end = len(audio.Samples)
		}
		if !onChunk(&Audio{SampleRate: audio.SampleRate, Samples: audio.Samples[start:end]}) {
			return nil
		}
	}
	return nil
}

func fallbackSamplesPerChunk(sampleRate int) int {
	samplesPerChunk := sampleRate * fallbackChunkMillis / 1000
	if samplesPerChunk < 1 {
		return 1
	}
	return samplesPerChunk
}

func fallbackChunkCount(totalSamples int, samplesPerChunk int) int {
	if totalSamples <= 0 || samplesPerChunk <= 0 {
		return 0
	}
	return (totalSamples + samplesPerChunk - 1) / samplesPerChunk
}
