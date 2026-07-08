package serial

import (
	"bufio"
	"encoding/json"
	"log"
	"strings"
	"sync"
	"time"

	"go.bug.st/serial"
)

type Config struct {
	Port string
	Baud int
}

type Reader struct {
	cfg      Config
	mu       sync.Mutex
	latest   map[string]interface{}
	dataChan chan map[string]interface{}
	stopCh   chan struct{}
}

func NewReader(cfg Config) *Reader {
	if cfg.Baud == 0 {
		cfg.Baud = 115200
	}
	return &Reader{
		cfg:      cfg,
		dataChan: make(chan map[string]interface{}, 16),
		stopCh:   make(chan struct{}),
	}
}

func (r *Reader) DataChan() <-chan map[string]interface{} {
	return r.dataChan
}

func (r *Reader) Latest() map[string]interface{} {
	r.mu.Lock()
	defer r.mu.Unlock()
	if r.latest == nil {
		return nil
	}
	cp := make(map[string]interface{}, len(r.latest))
	for k, v := range r.latest {
		cp[k] = v
	}
	return cp
}

func (r *Reader) Start() {
	go r.loop()
}

func (r *Reader) Stop() {
	close(r.stopCh)
}

func (r *Reader) loop() {
	for {
		select {
		case <-r.stopCh:
			return
		default:
		}

		port, err := serial.Open(r.cfg.Port, &serial.Mode{
			BaudRate: r.cfg.Baud,
		})
		if err != nil {
			log.Printf("[serial] cannot open %s: %v, retrying in 3s...", r.cfg.Port, err)
			select {
			case <-r.stopCh:
				return
			case <-time.After(3 * time.Second):
			}
			continue
		}
		log.Printf("[serial] opened %s @ %d baud", r.cfg.Port, r.cfg.Baud)

		r.readLoop(port)
		_ = port.Close()
		log.Printf("[serial] connection lost, reconnecting in 3s...")

		select {
		case <-r.stopCh:
			return
		case <-time.After(3 * time.Second):
		}
	}
}

func (r *Reader) readLoop(port serial.Port) {
	scanner := bufio.NewScanner(port)
	scanner.Buffer(make([]byte, 0, 64*1024), 64*1024)

	for scanner.Scan() {
		select {
		case <-r.stopCh:
			return
		default:
		}

		line := strings.TrimSpace(scanner.Text())
		if line == "" {
			continue
		}

		jsonStr := ""
		if strings.HasPrefix(line, "JSON:") {
			jsonStr = line[5:]
		} else if strings.HasPrefix(line, "{") {
			jsonStr = line
		} else {
			continue
		}

		var data map[string]interface{}
		if err := json.Unmarshal([]byte(jsonStr), &data); err != nil {
			log.Printf("[serial] invalid JSON: %v", err)
			continue
		}

		r.mu.Lock()
		r.latest = data
		r.mu.Unlock()

		select {
		case r.dataChan <- data:
		default:
			// drop if nobody is reading
		}
	}
}
