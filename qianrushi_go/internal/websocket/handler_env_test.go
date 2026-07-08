package websocket

import "testing"

func TestLocalEnvAnswerUsesPanelFieldNames(t *testing.T) {
	s := &session{
		envProvider: func() map[string]interface{} {
			return map[string]interface{}{
				"Enviroment_Temperation": 26.5,
				"Enviroment_Humidity":    61,
				"Enviroment_Light":       320,
				"Enviroment_Pm25":        12,
				"Enviroment_Pm10":        31,
				"Wind_Speed":             2.4,
				"Wind_Direction":         180,
			}
		},
	}

	tests := []struct {
		question string
		want     string
	}{
		{"现在湿度是多少", "当前空气湿度是61%。"},
		{"现在温度是多少", "当前环境温度是26.5摄氏度。"},
		{"光照强度是多少", "当前光照强度是320勒克斯。"},
		{"PM2.5是多少", "当前 PM2.5是12微克每立方米。"},
		{"PM10是多少", "当前 PM10是31微克每立方米。"},
		{"风速和风向是多少", "当前风速是2.4米每秒，风向是180度。"},
	}

	for _, tt := range tests {
		got, ok := s.localEnvAnswer(tt.question)
		if !ok {
			t.Fatalf("localEnvAnswer(%q) did not handle env question", tt.question)
		}
		if got != tt.want {
			t.Fatalf("localEnvAnswer(%q) = %q, want %q", tt.question, got, tt.want)
		}
	}
}

func TestLocalEnvAnswerDoesNotAskModelWhenEnvMissing(t *testing.T) {
	s := &session{envProvider: func() map[string]interface{} { return nil }}

	got, ok := s.localEnvAnswer("现在温度是多少")
	if !ok {
		t.Fatal("environment question should be handled locally")
	}
	want := "当前还没有收到环境传感器数据，请检查 STM32 串口连接。"
	if got != want {
		t.Fatalf("localEnvAnswer() = %q, want %q", got, want)
	}
}

func TestLocalEnvAnswerPrefersPanelSnapshot(t *testing.T) {
	s := &session{
		envProvider: func() map[string]interface{} {
			return map[string]interface{}{"Enviroment_Humidity": 10}
		},
	}
	s.setEnvSnapshot(map[string]interface{}{"Enviroment_Humidity": 66})

	got, ok := s.localEnvAnswer("现在湿度是多少")
	if !ok {
		t.Fatal("environment question should be handled locally")
	}
	want := "当前空气湿度是66%。"
	if got != want {
		t.Fatalf("localEnvAnswer() = %q, want %q", got, want)
	}
}

func TestEnvSnapshotWritesGlobalStoreOnly(t *testing.T) {
	store := NewEnvStore()
	s := &session{
		envStore: store,
	}

	s.setEnvSnapshot(map[string]interface{}{"Enviroment_Temperation": 27.5})

	latest := store.Latest()
	if got := latest["Enviroment_Temperation"]; got != 27.5 {
		t.Fatalf("global latest temperature=%v, want 27.5", got)
	}
}

func TestLocalEnvAnswerFallsBackToGlobalStore(t *testing.T) {
	store := NewEnvStore()
	store.Set(map[string]interface{}{"Enviroment_Temperation": 28.5})
	s := &session{
		envStore: store,
		envProvider: func() map[string]interface{} {
			return map[string]interface{}{"Enviroment_Temperation": 11}
		},
	}

	got, ok := s.localEnvAnswer("现在温度多少")
	if !ok {
		t.Fatal("environment question should be handled locally")
	}
	want := "当前环境温度是28.5摄氏度。"
	if got != want {
		t.Fatalf("localEnvAnswer() = %q, want %q", got, want)
	}
}

func TestEnvSnapshotWithoutPublisherStillAnswers(t *testing.T) {
	s := &session{envStore: NewEnvStore()}
	s.setEnvSnapshot(map[string]interface{}{"Enviroment_Humidity": 55})

	got, ok := s.localEnvAnswer("现在湿度怎么样")
	if !ok {
		t.Fatal("environment question should be handled locally")
	}
	want := "当前空气湿度是55%。"
	if got != want {
		t.Fatalf("localEnvAnswer() = %q, want %q", got, want)
	}
}
