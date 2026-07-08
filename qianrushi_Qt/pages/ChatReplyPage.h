// 对话回复页面 - 展示用户提问和AI助手的回复内容
#pragma once

#include <QWidget>

#include "models/POIInfo.h"
#include "models/RouteInfo.h"

class QFrame;
class QLabel;
class QPushButton;
class MockChatService;
class MockDataService;
class NavigationController;
class MapWidget;
class VoiceButton;
class VoiceClient;

// 对话回复页面，显示用户提问气泡、AI回答和快捷追问入口
class ChatReplyPage : public QWidget
{
    Q_OBJECT

public:
    ChatReplyPage(NavigationController *nav,
                  MockDataService *data,
                  MockChatService *chat,
                  VoiceClient *voiceClient,
                  QWidget *parent = nullptr);
    // 刷新页面显示，加载最新的问答内容
    void refresh();

private:
    // 发起新对话并跳转
    void ask(const QString &text);
    void applyVoiceReply(const QString &answer);
    void applyMapToolResult(const QString &query);
    void prepareRoutePreview(const POIInfo &poi);
    void applyRouteResult(const POIInfo &poi, const RouteInfo &route);
    void updateRoutePreview();

    NavigationController *m_nav = nullptr;      // 导航控制器
    MockDataService *m_data = nullptr;          // 数据服务
    MockChatService *m_chat = nullptr;          // 聊天服务
    QLabel *m_userBubble = nullptr;             // 用户提问气泡
    QLabel *m_answer = nullptr;                 // AI回答文本
    QLabel *m_meta = nullptr;                   // 目的地元信息
    QLabel *m_routeTitle = nullptr;
    QLabel *m_routeSummary = nullptr;
    QLabel *m_routeFeature = nullptr;
    QFrame *m_routeCard = nullptr;
    MapWidget *m_routeMap = nullptr;
    QPushButton *m_routeDetailButton = nullptr;
    QLabel *m_voiceStatus = nullptr;
    QLabel *m_playbackStatus = nullptr;
    VoiceButton *m_voiceButton = nullptr;       // 语音输入按钮
    VoiceClient *m_voiceClient = nullptr;
    QString m_pendingVoiceQuestion;
    int m_pendingRouteRequestId = 0;
};
