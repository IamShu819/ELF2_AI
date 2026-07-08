package rag

import (
	"sort"
	"strings"
)

const (
	CoverFacilityLocation     = "facility_location"
	CoverSpecificRoomLocation = "specific_room_location"
	CoverServiceProcedure     = "service_procedure"
	CoverSafetyGuidance       = "safety_guidance"
	CoverEmergencyGuidance    = "emergency_guidance"
	CoverEquipmentOperation   = "equipment_operation"
	CoverCapability           = "capability"
	CoverContactPhone         = "contact_phone"
	CoverCredential           = "credential"
	CoverOpeningHours         = "opening_hours"
	CoverLiveStatus           = "live_status"
	CoverPersonalStatus       = "personal_status"
	CoverBooking              = "booking"
	CoverMenu                 = "menu"
	CoverPersonIdentity       = "person_identity"
	CoverIdentifier           = "identifier"
	CoverSchedule             = "schedule"
)

var allowedCovers = map[string]struct{}{
	CoverFacilityLocation:     {},
	CoverSpecificRoomLocation: {},
	CoverServiceProcedure:     {},
	CoverSafetyGuidance:       {},
	CoverEmergencyGuidance:    {},
	CoverEquipmentOperation:   {},
	CoverCapability:           {},
	CoverContactPhone:         {},
	CoverCredential:           {},
	CoverOpeningHours:         {},
	CoverLiveStatus:           {},
	CoverPersonalStatus:       {},
	CoverBooking:              {},
	CoverMenu:                 {},
	CoverPersonIdentity:       {},
	CoverIdentifier:           {},
	CoverSchedule:             {},
}

func IsAllowedCover(cover string) bool {
	_, ok := allowedCovers[strings.TrimSpace(cover)]
	return ok
}

type Answerability struct {
	Answerable      bool
	RequiredCovers  []string
	MatchedCovers   []string
	RawResults      []Result
	EligibleHitIDs  []string
	EligibleResults []Result
	DecisionReason  string
}

func IsDomainFactQuestion(question string) bool {
	_, ok := RequiredCovers(question)
	return ok
}

func RequiredCovers(question string) ([]string, bool) {
	q := strings.TrimSpace(question)
	if q == "" {
		return nil, false
	}
	covers := make([]string, 0, 2)
	add := func(cover string) {
		for _, existing := range covers {
			if existing == cover {
				return
			}
		}
		covers = append(covers, cover)
	}

	// Precise fact types take priority over general location/procedure signals.
	if containsAny(q, "电话", "联系方式", "联系电话", "值班电话", "医院电话") {
		add(CoverContactPhone)
	}
	if containsAny(q, "wifi", "wi-fi", "密码", "账号", "口令") {
		add(CoverCredential)
	}
	if containsAny(q, "开放时间", "几点下班", "几点关门", "营业时间", "上班时间") {
		add(CoverOpeningHours)
	}
	if containsAny(q, "审批状态", "办理进度", "我的访客", "我的申请") {
		add(CoverPersonalStatus)
	}
	if containsAny(q, "预约") {
		add(CoverBooking)
	}
	if containsAny(q, "菜单") {
		add(CoverMenu)
	}
	if containsAny(q, "负责人", "责任人", "联系人姓名", "负责人姓名") {
		add(CoverPersonIdentity)
	}
	if containsAny(q, "编号", "设备编号", "快递柜编号", "门牌号") {
		add(CoverIdentifier)
	}
	if containsAny(q, "时刻表", "班次", "日程安排") {
		add(CoverSchedule)
	}
	if containsAny(q, "剩几个", "还剩", "实时", "当前车位", "今天停车场") {
		add(CoverLiveStatus)
	}
	if containsAny(q, "楼栋", "楼层", "哪层", "几层", "房间", "办公室", "门牌") && containsAny(q, "在哪里", "在哪", "哪层", "位置") {
		add(CoverSpecificRoomLocation)
	}
	if len(covers) > 0 {
		return normalizeCovers(covers), true
	}

	if containsAny(q, "灭火器", "消防器材", "烟雾", "烟味", "消防通道", "安全出口", "用电", "裸露电线", "设备围挡", "围栏", "夜间通行", "老人小孩", "特殊人群", "安全规范") {
		return []string{CoverSafetyGuidance}, true
	}
	if containsAny(q, "火情", "着火", "受伤", "摔倒", "流血", "迷路", "找不到路", "设备异常", "异响", "恶劣天气", "暴雨", "大风", "避险", "应急") {
		return []string{CoverEmergencyGuidance}, true
	}
	if containsAny(q, "终端", "语音按钮", "点击说话", "正在聆听", "麦克风按钮", "语音无响应", "没反应", "地图导航", "监测页面", "传感器数据", "音量", "播放", "播报", "离线", "断网", "掉线") {
		return []string{CoverEquipmentOperation}, true
	}
	if containsAny(q, "系统能做什么", "有哪些功能", "没有资料", "无资料", "查不到", "乱说") {
		return []string{CoverCapability}, true
	}
	if containsAny(q, "访客怎么登记", "登记入园", "签到", "临时通行", "临时访客证", "临时证", "通行证", "怎么办理", "失物招领", "丢东西", "落东西", "人工问询", "咨询服务") {
		return []string{CoverServiceProcedure}, true
	}
	if containsAny(q, "访客中心", "服务台", "服务大厅", "卫生间", "厕所", "休息区", "坐一下", "大门", "入口", "出口", "停车", "接驳", "班车", "临停", "无障碍", "轮椅", "饮水", "喝水", "充电", "服务台问路") {
		return []string{CoverFacilityLocation}, true
	}
	if containsAny(q, "园区服务") && containsAny(q, "在哪里", "在哪", "怎么走") {
		return []string{CoverFacilityLocation}, true
	}
	if containsAny(q, "园区", "访客", "来访", "服务", "安全", "应急", "消防", "设备", "巡检") && containsAny(q, "在哪里", "在哪", "怎么", "如何", "怎么办", "有哪些", "是什么", "是多少", "能不能", "可以") {
		return nil, true
	}
	return nil, false
}

func EvaluateAnswerability(results []Result, required []string) Answerability {
	required = normalizeCovers(required)
	if len(required) == 0 {
		return Answerability{Answerable: false, RawResults: results, DecisionReason: "required_covers_unrecognized"}
	}
	matchedSet := make(map[string]struct{}, len(required))
	for _, result := range results {
		itemCovers := coverSet(result.Item.Covers)
		for _, req := range required {
			if _, ok := itemCovers[req]; ok {
				matchedSet[req] = struct{}{}
			}
		}
	}
	matched := make([]string, 0, len(matchedSet))
	for _, req := range required {
		if _, ok := matchedSet[req]; ok {
			matched = append(matched, req)
		}
	}
	if len(matched) != len(required) {
		return Answerability{Answerable: false, RequiredCovers: required, MatchedCovers: matched, RawResults: results, DecisionReason: "required_cover_missing"}
	}
	requiredSet := coverSet(required)
	eligible := make([]Result, 0, len(results))
	ids := make([]string, 0, len(results))
	for _, result := range results {
		if coversIntersect(result.Item.Covers, requiredSet) {
			eligible = append(eligible, result)
			ids = append(ids, result.Item.ID)
		}
	}
	return Answerability{Answerable: true, RequiredCovers: required, MatchedCovers: matched, RawResults: results, EligibleHitIDs: ids, EligibleResults: eligible, DecisionReason: "answerable"}
}

func coversIntersect(covers []string, requiredSet map[string]struct{}) bool {
	for _, cover := range covers {
		if _, ok := requiredSet[strings.TrimSpace(cover)]; ok {
			return true
		}
	}
	return false
}

func normalizeCovers(covers []string) []string {
	seen := map[string]struct{}{}
	out := make([]string, 0, len(covers))
	for _, cover := range covers {
		cover = strings.TrimSpace(cover)
		if cover == "" {
			continue
		}
		if _, ok := seen[cover]; ok {
			continue
		}
		seen[cover] = struct{}{}
		out = append(out, cover)
	}
	sort.SliceStable(out, func(i, j int) bool { return coverOrder(out[i]) < coverOrder(out[j]) })
	return out
}

func coverSet(covers []string) map[string]struct{} {
	set := make(map[string]struct{}, len(covers))
	for _, cover := range covers {
		set[strings.TrimSpace(cover)] = struct{}{}
	}
	return set
}

func coverOrder(cover string) int {
	order := []string{CoverFacilityLocation, CoverSpecificRoomLocation, CoverServiceProcedure, CoverSafetyGuidance, CoverEmergencyGuidance, CoverEquipmentOperation, CoverCapability, CoverContactPhone, CoverCredential, CoverOpeningHours, CoverLiveStatus, CoverPersonalStatus, CoverBooking, CoverMenu, CoverPersonIdentity, CoverIdentifier, CoverSchedule}
	for i, value := range order {
		if cover == value {
			return i
		}
	}
	return len(order)
}

func containsAny(text string, keywords ...string) bool {
	text = strings.ToLower(text)
	for _, keyword := range keywords {
		if strings.Contains(text, strings.ToLower(keyword)) {
			return true
		}
	}
	return false
}
