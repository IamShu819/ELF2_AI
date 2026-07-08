package llm

import (
	"strings"
	"testing"
)

func TestCleanRKLLMOutputDropsRuntimeLogsAndThinkBlock(t *testing.T) {
	const question = "今天天气怎么样"
	const raw = `
rkllm init start
I rkllm: rkllm-runtime version: 1.1.4, rknpu driver version: 0.9.8, platform: RK3588
rkllm init success
用户：今天天气怎么样
助手：
<think>
这里是推理过程，不应该播报。
</think>
今天天气不错，适合出门。
`

	got := cleanRKLLMOutput(raw, question, true)
	if got != "今天天气不错，适合出门。" {
		t.Fatalf("unexpected cleaned output: %q", got)
	}
}

func TestSplitForStreamingKeepsSentenceBoundaries(t *testing.T) {
	parts := splitForStreaming("第一句。第二句！最后")
	joined := strings.Join(parts, "")
	if joined != "第一句。第二句！最后" {
		t.Fatalf("unexpected stream parts: %#v", parts)
	}
	if len(parts) != 3 {
		t.Fatalf("expected 3 parts, got %d: %#v", len(parts), parts)
	}
}

func TestCleanRKLLMOutputDropsDemoMenu(t *testing.T) {
	const question = "你好"
	const raw = `
rkllm init success

**********************可输入以下问题对应序号获取回答/或自定义输入********************

[0] 现有一笼子，里面有鸡和兔子若干只，数一数，共有头14个，腿38条，求鸡和兔子各有多少只？
[1] 有28位小朋友排成一行,从左边开始数第10位是学豆,从右边开始数他是第几位?

*************************************************************************

你好，我是智能便民导航助手。
`

	got := cleanRKLLMOutput(raw, question, true)
	if got != "你好，我是智能便民导航助手。" {
		t.Fatalf("demo menu leaked into answer: %q", got)
	}
}

func TestCleanRKLLMOutputStripsRoleAndMarkers(t *testing.T) {
	got := cleanRKLLMOutput("robot: 下午两点三刻 <<", "现在几点", true)
	if got != "下午两点三刻" {
		t.Fatalf("unexpected cleaned output: %q", got)
	}
}

func TestCleanRKLLMOutputTruncatesQwenChatTemplateContinuation(t *testing.T) {
	raw := "好的，那我们继续聊天吧！有什么想聊的吗？<｜endofsentence｜> 好的，那我们继续聊天吧！有什么想聊的吗？<｜begin▁of▁sentence｜><｜User｜>你叫什么名字？<｜endofsentence｜> 我叫小助手。"
	got := cleanRKLLMOutput(raw, "你是谁", true)
	want := "好的，那我们继续聊天吧！有什么想聊的吗？"
	if got != want {
		t.Fatalf("expected %q, got %q", want, got)
	}
}

func TestTruncateAtRKLLMStopTokenHandlesLowercaseUserPrefix(t *testing.T) {
	raw := "抱歉，我无法提供实时天气信息。<｜user:"
	got := truncateAtRKLLMStopToken(raw)
	want := "抱歉，我无法提供实时天气信息。"
	if got != want {
		t.Fatalf("expected %q, got %q", want, got)
	}
}

func TestTruncateAtRKLLMStopTokenHandlesResidualTemplateFragments(t *testing.T) {
	cases := map[string]string{
		"回答。endofsentence｜>助手：不客气":         "回答。",
		"回答。<｜beginofsentence｜":            "回答。",
		"回答。<｜begin▁of▁sentence｜><｜User｜>": "回答。",
	}
	for raw, want := range cases {
		if got := truncateAtRKLLMStopToken(raw); got != want {
			t.Fatalf("for %q expected %q, got %q", raw, want, got)
		}
	}
}

func TestRKLLMFailureMessageCatchesRuntimeFailure(t *testing.T) {
	raw := `rknn_init_fail! ret=-1
E RKNN: failed to allocate handle
failed to malloc npu memory
load model file error!`
	if got := rkllmFailureMessage(raw); got == "" {
		t.Fatal("expected RKLLM failure message")
	}
	if cleaned := cleanRKLLMOutput(raw, "", true); cleaned != "" {
		t.Fatalf("runtime failure leaked into answer: %q", cleaned)
	}
}
