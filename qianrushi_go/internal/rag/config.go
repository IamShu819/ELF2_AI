package rag

const (
	DefaultTopK            = 3
	DefaultMinScore        = 0.55
	DefaultMaxItemRunes    = 800
	DefaultMaxContextRunes = 2400
)

// Config controls retrieval and context assembly.
// DefaultMinScore is the formal RAG answerability threshold selected by
// stage-005B real bge-small-zh-v1.5 evaluation. MinScoreSet
// distinguishes an omitted MinScore from an explicit zero threshold.
type Config struct {
	TopK            int
	MinScore        float64
	MinScoreSet     bool
	MaxItemRunes    int
	MaxContextRunes int
}

func DefaultConfig() Config {
	return Config{
		TopK:            DefaultTopK,
		MinScore:        DefaultMinScore,
		MinScoreSet:     true,
		MaxItemRunes:    DefaultMaxItemRunes,
		MaxContextRunes: DefaultMaxContextRunes,
	}
}

func (c Config) withDefaults() Config {
	d := DefaultConfig()
	if c.TopK <= 0 {
		c.TopK = d.TopK
	}
	if !c.MinScoreSet {
		if c.MinScore == 0 {
			c.MinScore = d.MinScore
		}
		c.MinScoreSet = true
	}
	if c.MaxItemRunes <= 0 {
		c.MaxItemRunes = d.MaxItemRunes
	}
	if c.MaxContextRunes <= 0 {
		c.MaxContextRunes = d.MaxContextRunes
	}
	return c
}
