package mqtt

import (
	"bytes"
	"encoding/json"
	"fmt"
	"log"
	"sync"
	"time"

	mqttlib "github.com/eclipse/paho.mqtt.golang"
)

const (
	TopicData = "test"
	TopicWarm = "warm"
	TopicCmd  = "massge"

	publishQueueCapacity = 128
	telemetryQoS         = 0
	telemetryTimeout     = 500 * time.Millisecond
	telemetryMinInterval = 100 * time.Millisecond
	rawEventQoS          = 1
	rawEventTimeout      = 3 * time.Second
	logLimitInterval     = 5 * time.Second
)

type Config struct {
	Broker   string
	Port     int
	Username string
	Password string
}

type WarmMessage struct {
	Code    string `json:"code"`
	Active  bool   `json:"active"`
	Message string `json:"message"`
}

type OnWarmFunc func(code string)
type OnCmdPayloadFunc func(payload []byte)

type publishMessage struct {
	topic     string
	payload   []byte
	qos       byte
	timeout   time.Duration
	telemetry bool
}

type Client struct {
	cfg                     Config
	client                  mqttlib.Client
	mu                      sync.Mutex
	onWarm                  OnWarmFunc
	onCmdRaw                OnCmdPayloadFunc
	pubChan                 chan publishMessage
	stopCh                  chan struct{}
	stopOnce                sync.Once
	subscribeFunc           func() bool
	connected               bool
	publishLoopStarted      bool
	subscribeRetryRunning   bool
	lastConnectionLostLog   time.Time
	lastDisconnectedDropLog time.Time
	lastQueueLog            time.Time
}

func NewClient(cfg Config) *Client {
	if cfg.Port == 0 {
		cfg.Port = 1883
	}
	return &Client{
		cfg:     cfg,
		pubChan: make(chan publishMessage, publishQueueCapacity),
		stopCh:  make(chan struct{}),
	}
}

func (c *Client) OnWarm(fn OnWarmFunc) {
	c.mu.Lock()
	defer c.mu.Unlock()
	c.onWarm = fn
}

func (c *Client) OnCmdPayload(fn OnCmdPayloadFunc) {
	c.mu.Lock()
	defer c.mu.Unlock()
	c.onCmdRaw = fn
}

func (c *Client) IsConnected() bool {
	return c.isConnected()
}

func (c *Client) Start() error {
	broker := fmt.Sprintf("tcp://%s:%d", c.cfg.Broker, c.cfg.Port)
	opts := mqttlib.NewClientOptions().
		AddBroker(broker).
		SetAutoReconnect(true).
		SetConnectRetry(true).
		SetConnectRetryInterval(5 * time.Second).
		SetMaxReconnectInterval(30 * time.Second).
		SetKeepAlive(30 * time.Second).
		SetPingTimeout(10 * time.Second)

	if c.cfg.Username != "" {
		opts.SetUsername(c.cfg.Username)
	}
	if c.cfg.Password != "" {
		opts.SetPassword(c.cfg.Password)
	}

	opts.SetConnectionLostHandler(func(client mqttlib.Client, err error) {
		c.handleConnectionLost(err)
	})
	opts.SetOnConnectHandler(func(client mqttlib.Client) {
		c.handleConnected()
	})

	c.client = mqttlib.NewClient(opts)
	c.startPublishLoop()

	token := c.client.Connect()
	if !token.WaitTimeout(3 * time.Second) {
		log.Printf("[mqtt] initial connect timed out; background reconnect enabled")
		return nil
	}
	if err := token.Error(); err != nil {
		log.Printf("[mqtt] initial connect failed: %v; background reconnect enabled", err)
		return nil
	}

	return nil
}

func (c *Client) startPublishLoop() {
	c.mu.Lock()
	defer c.mu.Unlock()
	if c.publishLoopStarted {
		return
	}
	c.publishLoopStarted = true
	go c.publishLoop()
}

func (c *Client) Stop() {
	c.stopOnce.Do(func() {
		close(c.stopCh)
		c.setConnected(false)
		if c.client != nil {
			c.client.Disconnect(1000)
		}
	})
}

func (c *Client) requeueMessages(messages []publishMessage, reason string) {
	for _, msg := range messages {
		select {
		case c.pubChan <- msg:
		default:
			c.logQueueDrop(msg, reason)
		}
	}
}

func (c *Client) Publish(data map[string]interface{}) {
	payload, err := json.Marshal(data)
	if err != nil {
		log.Printf("[mqtt] marshal publish payload failed: %v", err)
		return
	}
	c.PublishRaw(payload)
}

func (c *Client) PublishRaw(payload []byte) {
	if len(bytes.TrimSpace(payload)) == 0 {
		log.Printf("[mqtt] empty raw publish payload ignored")
		return
	}
	telemetry := isEnvironmentTelemetry(payload)
	msg := publishMessage{
		topic:     TopicData,
		payload:   append([]byte(nil), payload...),
		qos:       rawEventQoS,
		timeout:   rawEventTimeout,
		telemetry: telemetry,
	}
	if telemetry {
		msg.qos = telemetryQoS
		msg.timeout = telemetryTimeout
	}
	c.enqueuePublish(msg)
}

func isEnvironmentTelemetry(payload []byte) bool {
	var obj map[string]interface{}
	if err := json.Unmarshal(bytes.TrimSpace(payload), &obj); err != nil || obj == nil {
		return false
	}
	for _, key := range environmentTelemetryFields {
		if _, ok := obj[key]; ok {
			return true
		}
	}
	return false
}

var environmentTelemetryFields = []string{
	"Enviroment_Temperation",
	"Enviroment_Humidity",
	"Enviroment_Light",
	"Enviroment_Pm25",
	"Enviroment_Pm10",
	"Wind_Speed",
	"Wind_Direction",
}

func (c *Client) enqueuePublish(msg publishMessage) {
	if !c.isConnected() {
		c.enqueueWhileDisconnected(msg)
		return
	}
	select {
	case c.pubChan <- msg:
		return
	default:
	}

	if dropped, ok := c.dropQueuedTelemetry(); ok {
		log.Printf("[mqtt] publish queue full, dropped telemetry topic=%s bytes=%d queue_capacity=%d", dropped.topic, len(dropped.payload), cap(c.pubChan))
		c.sendAfterDrop(msg)
		return
	}

	if msg.telemetry {
		log.Printf("[mqtt] publish queue full, dropped telemetry topic=%s bytes=%d queue_capacity=%d", msg.topic, len(msg.payload), cap(c.pubChan))
		return
	}

	select {
	case dropped := <-c.pubChan:
		log.Printf("[mqtt] ERROR publish queue full, dropped non-telemetry topic=%s bytes=%d queue_capacity=%d", dropped.topic, len(dropped.payload), cap(c.pubChan))
	default:
	}
	c.sendAfterDrop(msg)
}

func (c *Client) sendAfterDrop(msg publishMessage) {
	select {
	case c.pubChan <- msg:
	default:
		c.logQueueDrop(msg, "publish queue full")
	}
}

func (c *Client) dropQueuedTelemetry() (publishMessage, bool) {
	n := len(c.pubChan)
	if n == 0 {
		return publishMessage{}, false
	}
	kept := make([]publishMessage, 0, n)
	var dropped publishMessage
	droppedTelemetry := false
	for i := 0; i < n; i++ {
		select {
		case msg := <-c.pubChan:
			if msg.telemetry && !droppedTelemetry {
				dropped = msg
				droppedTelemetry = true
				continue
			}
			kept = append(kept, msg)
		default:
			i = n
		}
	}
	c.requeueMessages(kept, "publish queue refill full after telemetry drop")
	return dropped, droppedTelemetry
}

func (c *Client) enqueueWhileDisconnected(msg publishMessage) {
	c.logDisconnectedDrop(msg)
}

func (c *Client) handleConnected() {
	c.setConnected(true)
	if c.restoreSubscriptions() {
		log.Printf("[mqtt] reconnected and subscriptions restored")
		return
	}
	c.startSubscribeRetry()
}

func (c *Client) handleConnectionLost(err error) {
	c.setConnected(false)
	c.logConnectionLost(err)
}

func (c *Client) restoreSubscriptions() bool {
	c.mu.Lock()
	fn := c.subscribeFunc
	c.mu.Unlock()
	if fn != nil {
		return fn()
	}
	return c.subscribe()
}

func (c *Client) startSubscribeRetry() {
	c.mu.Lock()
	if c.subscribeRetryRunning {
		c.mu.Unlock()
		return
	}
	c.subscribeRetryRunning = true
	c.mu.Unlock()

	go func() {
		ticker := time.NewTicker(5 * time.Second)
		defer ticker.Stop()
		defer func() {
			c.mu.Lock()
			c.subscribeRetryRunning = false
			c.mu.Unlock()
		}()

		for {
			select {
			case <-c.stopCh:
				return
			case <-ticker.C:
				if !c.isConnected() {
					return
				}
				if c.restoreSubscriptions() {
					log.Printf("[mqtt] reconnected and subscriptions restored")
					return
				}
			}
		}
	}()
}

func (c *Client) PublishWarm(code string) {
	payload := []byte(code)
	if len(bytes.TrimSpace(payload)) == 0 {
		log.Printf("[mqtt] empty warm publish payload ignored")
		return
	}
	c.enqueuePublish(publishMessage{
		topic:   TopicWarm,
		payload: payload,
		qos:     rawEventQoS,
		timeout: rawEventTimeout,
	})
}

func (c *Client) handleCmdPayload(payload []byte) {
	payload = append([]byte(nil), payload...)
	log.Printf("[mqtt] massge received: bytes=%d", len(payload))
	if len(bytes.TrimSpace(payload)) == 0 {
		log.Printf("[mqtt] empty massge command ignored")
		return
	}

	c.mu.Lock()
	rawFn := c.onCmdRaw
	c.mu.Unlock()
	if rawFn == nil {
		log.Printf("[mqtt] no massge raw command handler configured")
		return
	}
	rawFn(payload)
	log.Printf("[mqtt] massge command forwarded as raw STM32 command, bytes=%d", len(payload))
}

func (c *Client) subscribe() bool {
	ok := true
	warmToken := c.client.Subscribe(TopicWarm, 1, func(client mqttlib.Client, msg mqttlib.Message) {
		code := string(msg.Payload())
		log.Printf("[mqtt] warm received: %s", code)
		c.mu.Lock()
		fn := c.onWarm
		c.mu.Unlock()
		if fn != nil {
			fn(code)
		}
	})
	if !warmToken.WaitTimeout(3 * time.Second) {
		log.Printf("[mqtt] subscribe %s timed out", TopicWarm)
		ok = false
	} else if err := warmToken.Error(); err != nil {
		log.Printf("[mqtt] subscribe %s failed: %v", TopicWarm, err)
		ok = false
	} else {
		log.Printf("[mqtt] subscribed to %s", TopicWarm)
	}

	cmdToken := c.client.Subscribe(TopicCmd, 1, func(client mqttlib.Client, msg mqttlib.Message) {
		c.handleCmdPayload(msg.Payload())
	})
	if !cmdToken.WaitTimeout(3 * time.Second) {
		log.Printf("[mqtt] subscribe %s timed out", TopicCmd)
		ok = false
	} else if err := cmdToken.Error(); err != nil {
		log.Printf("[mqtt] subscribe %s failed: %v", TopicCmd, err)
		ok = false
	} else {
		log.Printf("[mqtt] subscribed to %s", TopicCmd)
	}
	return ok
}

func (c *Client) publishLoop() {
	var lastTopicDataPublish time.Time
	for {
		select {
		case <-c.stopCh:
			return
		case msg := <-c.pubChan:
			if msg.telemetry {
				msg = c.coalesceLatestTelemetry(msg)
				if wait := telemetryMinInterval - time.Since(lastTopicDataPublish); wait > 0 {
					timer := time.NewTimer(wait)
					select {
					case <-c.stopCh:
						timer.Stop()
						return
					case <-timer.C:
					}
				}
				lastTopicDataPublish = time.Now()
			}
			c.publishTopic(msg)
		}
	}
}

func (c *Client) coalesceLatestTelemetry(msg publishMessage) publishMessage {
	n := len(c.pubChan)
	if n == 0 {
		return msg
	}
	dropped := 0
	kept := make([]publishMessage, 0, n)
	for i := 0; i < n; i++ {
		select {
		case next := <-c.pubChan:
			if next.telemetry {
				dropped++
				msg = next
				continue
			}
			kept = append(kept, next)
		default:
			i = n
		}
	}
	c.requeueMessages(kept, "publish queue refill full after telemetry coalesce")
	if dropped > 0 {
		log.Printf("[mqtt] coalesced stale telemetry publishes topic=%s dropped=%d queue_capacity=%d", TopicData, dropped, cap(c.pubChan))
	}
	return msg
}

func (c *Client) publishTopic(msg publishMessage) {
	topic := msg.topic
	payload := msg.payload
	qos := msg.qos
	timeout := msg.timeout
	if c.client == nil || !c.isConnected() || !c.client.IsConnected() {
		c.logDisconnectedDrop(msg)
		return
	}
	if timeout <= 0 {
		timeout = 3 * time.Second
	}
	token := c.client.Publish(topic, qos, false, payload)
	if !token.WaitTimeout(timeout) {
		c.logPublishFailure(topic, len(payload), "timed out")
		return
	}
	if err := token.Error(); err != nil {
		c.logPublishFailure(topic, len(payload), err.Error())
		return
	}
	if topic != TopicData || qos != telemetryQoS {
		log.Printf("[mqtt] published topic=%s bytes=%d qos=%d", topic, len(payload), qos)
	}
}

func (c *Client) setConnected(connected bool) {
	c.mu.Lock()
	c.connected = connected
	c.mu.Unlock()
}

func (c *Client) isConnected() bool {
	c.mu.Lock()
	defer c.mu.Unlock()
	return c.connected
}

func (c *Client) logConnectionLost(err error) {
	c.mu.Lock()
	message := "[mqtt] connection lost, reconnecting in background"
	if err != nil {
		message = fmt.Sprintf("%s: %v", message, err)
	}
	c.lastConnectionLostLog = c.logLimitedLocked(c.lastConnectionLostLog, message)
	c.mu.Unlock()
}

func (c *Client) logDisconnectedDrop(msg publishMessage) {
	c.mu.Lock()
	c.lastDisconnectedDropLog = c.logLimitedLocked(
		c.lastDisconnectedDropLog,
		fmt.Sprintf("[mqtt] disconnected, dropped publish topic=%s bytes=%d", msg.topic, len(msg.payload)),
	)
	c.mu.Unlock()
}

func (c *Client) logQueueDrop(msg publishMessage, reason string) {
	c.mu.Lock()
	c.lastQueueLog = c.logLimitedLocked(
		c.lastQueueLog,
		fmt.Sprintf("[mqtt] %s, dropped topic=%s bytes=%d queue_capacity=%d", reason, msg.topic, len(msg.payload), cap(c.pubChan)),
	)
	c.mu.Unlock()
}

func (c *Client) logPublishFailure(topic string, size int, reason string) {
	c.mu.Lock()
	c.lastDisconnectedDropLog = c.logLimitedLocked(
		c.lastDisconnectedDropLog,
		fmt.Sprintf("[mqtt] publish topic=%s failed/skipped: %s bytes=%d", topic, reason, size),
	)
	c.mu.Unlock()
}

func (c *Client) logLimitedLocked(last time.Time, message string) time.Time {
	now := time.Now()
	if now.Sub(last) >= logLimitInterval {
		log.Print(message)
		return now
	}
	return last
}
