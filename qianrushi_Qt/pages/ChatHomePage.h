// 对话首页 - 提供语音交互入口
#pragma once

#include <QWidget>

class MockChatService;
class MockDataService;
class NavigationController;
class QLabel;
class VoiceButton;
class VoiceClient;
class QFrame;

// 对话首页页面，展示吉祥物和语音按钮
class ChatHomePage : public QWidget
{
    Q_OBJECT

public:
    ChatHomePage(NavigationController *nav,
                 MockDataService *data,
                 MockChatService *chat,
                 VoiceClient *voiceClient,
                 QWidget *parent = nullptr);

private:
    NavigationController *m_nav = nullptr;      // 导航控制器
    MockDataService *m_data = nullptr;          // 数据服务
    MockChatService *m_chat = nullptr;          // 聊天服务
    QFrame *m_card = nullptr;                   // 页面卡片容器
    VoiceButton *m_voiceButton = nullptr;       // 语音按钮
    QLabel *m_voiceHint = nullptr;
    VoiceClient *m_voiceClient = nullptr;
};
