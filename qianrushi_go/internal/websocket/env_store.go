package websocket

import "sync"

// EnvStore keeps the latest environment snapshot shared across voice sessions.
type EnvStore struct {
	mu     sync.RWMutex
	latest map[string]interface{}
}

func NewEnvStore() *EnvStore {
	return &EnvStore{}
}

func (s *EnvStore) Set(data map[string]interface{}) {
	if s == nil || len(data) == 0 {
		return
	}
	cp := copyEnvMap(data)
	s.mu.Lock()
	s.latest = cp
	s.mu.Unlock()
}

func (s *EnvStore) Latest() map[string]interface{} {
	if s == nil {
		return nil
	}
	s.mu.RLock()
	defer s.mu.RUnlock()
	return copyEnvMap(s.latest)
}

func copyEnvMap(data map[string]interface{}) map[string]interface{} {
	if len(data) == 0 {
		return nil
	}
	cp := make(map[string]interface{}, len(data))
	for key, value := range data {
		cp[key] = value
	}
	return cp
}
