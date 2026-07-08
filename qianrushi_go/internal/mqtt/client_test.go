package mqtt

import (
	"strings"
	"testing"
	"time"
)

func TestMassgeTargetTypeDataSpeakIDPayloadForwardedRaw(t *testing.T) {
	payload := []byte(`{"Target":"SetPusher","Type":"string","Data":"extend","SpeakId":201}`)
	client := NewClient(Config{})

	var got []byte
	client.OnCmdPayload(func(payload []byte) {
		got = append([]byte(nil), payload...)
	})

	client.handleCmdPayload(payload)
	if string(got) != string(payload) {
		t.Fatalf("forwarded payload = %q, want raw %q", string(got), string(payload))
	}
}

func TestMassgeEmptyPayloadIgnored(t *testing.T) {
	client := NewClient(Config{})
	called := false
	client.OnCmdPayload(func(payload []byte) {
		called = true
	})

	client.handleCmdPayload([]byte(" \n\t"))
	if called {
		t.Fatal("empty massge payload should not be forwarded")
	}
}

func TestPublishQueueCapacityIsBounded(t *testing.T) {
	client := NewClient(Config{})
	if publishQueueCapacity != 128 {
		t.Fatalf("publishQueueCapacity=%d, want 128", publishQueueCapacity)
	}
	if got := cap(client.pubChan); got != publishQueueCapacity {
		t.Fatalf("publish queue capacity=%d, want %d", got, publishQueueCapacity)
	}
}

func TestPublishRawClassifiesTelemetryAndRawEvent(t *testing.T) {
	client := NewClient(Config{})
	client.setConnected(true)
	client.PublishRaw([]byte(`{"Enviroment_Temperation":27.5}`))
	client.PublishRaw([]byte(`{"Warn":"fire","SpeakId":301}`))

	telemetry := <-client.pubChan
	if !telemetry.telemetry || telemetry.qos != telemetryQoS || telemetry.timeout != telemetryTimeout {
		t.Fatalf("telemetry metadata=%+v, want telemetry qos0 short timeout", telemetry)
	}

	event := <-client.pubChan
	if event.telemetry || event.qos != rawEventQoS || event.timeout != rawEventTimeout {
		t.Fatalf("raw event metadata=%+v, want non-telemetry qos1 long timeout", event)
	}
}

func TestTelemetryJSONCoalescesKeepsLatest(t *testing.T) {
	client := NewClient(Config{})
	client.setConnected(true)
	first := publishMessage{topic: TopicData, payload: []byte(`{"Enviroment_Temperation":1}`), qos: telemetryQoS, timeout: telemetryTimeout, telemetry: true}
	client.enqueuePublish(publishMessage{topic: TopicData, payload: []byte(`{"Enviroment_Temperation":2}`), qos: telemetryQoS, timeout: telemetryTimeout, telemetry: true})
	client.enqueuePublish(publishMessage{topic: TopicData, payload: []byte(`{"Enviroment_Humidity":3}`), qos: telemetryQoS, timeout: telemetryTimeout, telemetry: true})

	got := client.coalesceLatestTelemetry(first)
	if string(got.payload) != `{"Enviroment_Humidity":3}` {
		t.Fatalf("coalesced payload=%q, want latest telemetry", string(got.payload))
	}
	if !got.telemetry || got.qos != telemetryQoS || got.timeout != telemetryTimeout {
		t.Fatalf("coalesced metadata=%+v, want telemetry qos0 short timeout", got)
	}
	if len(client.pubChan) != 0 {
		t.Fatalf("queue len after telemetry coalesce=%d, want 0", len(client.pubChan))
	}
}

func TestAlarmThenTelemetryPreservesAlarm(t *testing.T) {
	client := NewClient(Config{})
	client.setConnected(true)
	alarm := `{"Warn":"fire","SpeakId":301}`
	env := `{"Enviroment_Temperation":28.1}`
	client.PublishRaw([]byte(alarm))
	client.PublishRaw([]byte(env))

	first := <-client.pubChan
	if first.telemetry || string(first.payload) != alarm || first.qos != rawEventQoS {
		t.Fatalf("first queued=%+v payload=%q, want raw alarm event", first, string(first.payload))
	}
	second := <-client.pubChan
	if !second.telemetry || string(second.payload) != env || second.qos != telemetryQoS {
		t.Fatalf("second queued=%+v payload=%q, want telemetry after alarm", second, string(second.payload))
	}
}

func TestTelemetryThenAlarmRawEventNotCoalesced(t *testing.T) {
	client := NewClient(Config{})
	client.setConnected(true)
	telemetry := publishMessage{topic: TopicData, payload: []byte(`{"Enviroment_Light":100}`), qos: telemetryQoS, timeout: telemetryTimeout, telemetry: true}
	alarm := publishMessage{topic: TopicData, payload: []byte(`{"Warn":"smoke","SpeakId":302}`), qos: rawEventQoS, timeout: rawEventTimeout}
	client.enqueuePublish(alarm)

	got := client.coalesceLatestTelemetry(telemetry)
	if string(got.payload) != string(telemetry.payload) {
		t.Fatalf("coalesced telemetry=%q, want original telemetry", string(got.payload))
	}
	if len(client.pubChan) != 1 {
		t.Fatalf("queue len=%d, want alarm preserved", len(client.pubChan))
	}
	preserved := <-client.pubChan
	if preserved.telemetry || string(preserved.payload) != string(alarm.payload) || preserved.qos != rawEventQoS {
		t.Fatalf("preserved=%+v payload=%q, want raw alarm unchanged", preserved, string(preserved.payload))
	}
}

func TestQueueFullDropsTelemetryBeforeRawEvent(t *testing.T) {
	client := NewClient(Config{})
	client.setConnected(true)
	client.PublishRaw([]byte(`{"Enviroment_Temperation":1}`))
	for i := 1; i < publishQueueCapacity; i++ {
		client.PublishRaw([]byte(`{"Warn":"old","SpeakId":301}`))
	}
	client.PublishRaw([]byte(`{"Warn":"new","SpeakId":302}`))

	if got := len(client.pubChan); got != publishQueueCapacity {
		t.Fatalf("queue len=%d, want %d", got, publishQueueCapacity)
	}
	telemetryCount := 0
	newEventFound := false
	for len(client.pubChan) > 0 {
		msg := <-client.pubChan
		if msg.telemetry {
			telemetryCount++
		}
		if strings.Contains(string(msg.payload), `"SpeakId":302`) {
			newEventFound = true
		}
	}
	if telemetryCount != 0 {
		t.Fatalf("telemetry messages left=%d, want old telemetry dropped before raw events", telemetryCount)
	}
	if !newEventFound {
		t.Fatal("new raw event was not queued")
	}
}

func TestRequeueMessagesDoesNotBlockWhenChannelIsFull(t *testing.T) {
	client := NewClient(Config{})
	for i := 0; i < cap(client.pubChan); i++ {
		client.pubChan <- publishMessage{topic: TopicData, payload: []byte(`{"Warn":"old"}`)}
	}

	done := make(chan struct{})
	go func() {
		client.requeueMessages([]publishMessage{{topic: TopicData, payload: []byte(`{"Warn":"new"}`)}}, "test refill")
		close(done)
	}()

	select {
	case <-done:
	case <-time.After(500 * time.Millisecond):
		t.Fatal("requeueMessages blocked on full channel")
	}
}

func TestPublishWarmQueuesWarmQoS1(t *testing.T) {
	client := NewClient(Config{})
	client.setConnected(true)
	client.PublishWarm("sos")

	msg := <-client.pubChan
	if msg.topic != TopicWarm || string(msg.payload) != "sos" {
		t.Fatalf("warm msg topic=%s payload=%q, want warm/sos", msg.topic, string(msg.payload))
	}
	if msg.telemetry || msg.qos != rawEventQoS || msg.timeout != rawEventTimeout {
		t.Fatalf("warm msg metadata=%+v, want qos1 3s non-telemetry", msg)
	}
}

func TestPublishWhileDisconnectedDropsTelemetryAndRawTestEvents(t *testing.T) {
	client := NewClient(Config{})
	client.PublishRaw([]byte(`{"Enviroment_Temperation":27.5}`))
	client.PublishRaw([]byte(`{"Warn":"fire","SpeakId":301}`))

	if got := len(client.pubChan); got != 0 {
		t.Fatalf("pubChan len=%d, want disconnected test publishes dropped", got)
	}
}

func TestPublishWarmWhileDisconnectedIsDropped(t *testing.T) {
	client := NewClient(Config{})

	client.PublishWarm("sos")

	if got := len(client.pubChan); got != 0 {
		t.Fatalf("pubChan len=%d, want disconnected warm dropped", got)
	}
}

func TestReconnectRestoresSubscriptionsWithoutWarmReplay(t *testing.T) {
	client := NewClient(Config{})
	subscribeCount := 0
	client.subscribeFunc = func() bool {
		subscribeCount++
		return true
	}
	client.PublishWarm("sos")

	client.handleConnected()

	if !client.IsConnected() {
		t.Fatal("client should be marked connected")
	}
	if subscribeCount != 1 {
		t.Fatalf("subscribeCount=%d, want 1", subscribeCount)
	}
	if got := len(client.pubChan); got != 0 {
		t.Fatalf("pubChan len=%d, want no disconnected warm replay", got)
	}
}

func TestReconnectAfterDisconnectedSOSDoesNotReplayWarm(t *testing.T) {
	client := NewClient(Config{})
	client.PublishWarm("sos")
	client.PublishWarm("sos")

	client.subscribeFunc = func() bool {
		return true
	}
	client.handleConnected()

	if got := len(client.pubChan); got != 0 {
		t.Fatalf("pubChan len=%d, want no SOS replay after reconnect", got)
	}
}

func TestConnectionLostMarksDisconnected(t *testing.T) {
	client := NewClient(Config{})
	client.setConnected(true)

	client.handleConnectionLost(nil)

	if client.IsConnected() {
		t.Fatal("client should be marked disconnected")
	}
}

func TestDisconnectThenReconnectRestoresMassgeAndPublishes(t *testing.T) {
	client := NewClient(Config{})
	client.setConnected(true)
	client.handleConnectionLost(nil)
	client.PublishRaw([]byte(`{"Enviroment_Temperation":1}`))
	if got := len(client.pubChan); got != 0 {
		t.Fatalf("pubChan len while disconnected=%d, want telemetry dropped", got)
	}

	subscribeCount := 0
	client.subscribeFunc = func() bool {
		subscribeCount++
		return true
	}
	client.handleConnected()
	if subscribeCount != 1 {
		t.Fatalf("subscribeCount=%d, want 1", subscribeCount)
	}

	var gotCmd []byte
	client.OnCmdPayload(func(payload []byte) {
		gotCmd = append([]byte(nil), payload...)
	})
	cmd := []byte(`{"cmd":"test"}`)
	client.handleCmdPayload(cmd)
	if string(gotCmd) != string(cmd) {
		t.Fatalf("massge callback payload=%q, want %q", string(gotCmd), string(cmd))
	}

	client.PublishRaw([]byte(`{"Enviroment_Temperation":2}`))
	client.PublishWarm("sos")
	first := <-client.pubChan
	second := <-client.pubChan
	if first.topic != TopicData || !first.telemetry {
		t.Fatalf("first publish=%+v, want telemetry test", first)
	}
	if second.topic != TopicWarm || string(second.payload) != "sos" {
		t.Fatalf("second publish=%+v payload=%q, want warm/sos", second, string(second.payload))
	}
}

func TestPublishTopicWarmDisconnectedDoesNotQueueReplay(t *testing.T) {
	client := NewClient(Config{})
	client.setConnected(true)

	client.publishTopic(publishMessage{
		topic:   TopicWarm,
		payload: []byte("sos"),
		qos:     rawEventQoS,
		timeout: rawEventTimeout,
	})

	if got := len(client.pubChan); got != 0 {
		t.Fatalf("pubChan len=%d, want warm publish failure not queued", got)
	}
}

func TestInitialBrokerUnavailableDoesNotFailStart(t *testing.T) {
	client := NewClient(Config{Broker: "127.0.0.1", Port: 1})
	start := time.Now()
	if err := client.Start(); err != nil {
		t.Fatalf("Start returned err=%v, want nil for background reconnect", err)
	}
	defer client.Stop()
	if elapsed := time.Since(start); elapsed > 5*time.Second {
		t.Fatalf("Start elapsed=%s, want bounded initial connect timeout", elapsed)
	}
}

func TestConnectedWithSubscribeFailureStartsSingleRetry(t *testing.T) {
	client := NewClient(Config{})
	client.subscribeFunc = func() bool {
		return false
	}

	client.handleConnected()
	client.handleConnected()
	defer client.Stop()

	client.mu.Lock()
	running := client.subscribeRetryRunning
	client.mu.Unlock()
	if !running {
		t.Fatal("subscribe retry should be running after failed subscription restore")
	}
}

func TestStopIsIdempotent(t *testing.T) {
	client := NewClient(Config{})
	client.Stop()
	client.Stop()
}
