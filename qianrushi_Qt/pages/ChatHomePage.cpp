// 对话首页页面实现 - 包含吉祥物阴影和语音交互逻辑
#include "ChatHomePage.h"

#include <QFrame>
#include <QLabel>
#include <QPainter>
#include <QPushButton>
#include <QPixmap>
#include <QRadialGradient>
#include <QSizePolicy>
#include <QVBoxLayout>

#include "core/MockChatService.h"
#include "core/MockDataService.h"
#include "core/NavigationController.h"
#include "core/VoiceClient.h"
#include "widgets/VoiceButton.h"

namespace {
// 吉祥物阴影绘制组件
class MascotShadowWidget : public QWidget
{
public:
    explicit MascotShadowWidget(QWidget *parent = nullptr)
        : QWidget(parent)
    {
        setFixedSize(150, 28);
        setAttribute(Qt::WA_TransparentForMouseEvents);
    }

protected:
    // 绘制径向渐变阴影效果
    void paintEvent(QPaintEvent *) override
    {
        QPainter painter(this);
        painter.setRenderHint(QPainter::Antialiasing);
        QRadialGradient gradient(rect().center(), width() * 0.52, rect().center());
        gradient.setColorAt(0.0, QColor(15, 23, 42, 34));
        gradient.setColorAt(0.55, QColor(15, 23, 42, 18));
        gradient.setColorAt(1.0, QColor(15, 23, 42, 0));
        painter.setPen(Qt::NoPen);
        painter.setBrush(gradient);
        painter.drawEllipse(rect().adjusted(4, 4, -4, -4));
    }
};

}

// 构造函数，初始化页面布局和交互
ChatHomePage::ChatHomePage(NavigationController *nav,
                           MockDataService *data,
                           MockChatService *chat,
                           VoiceClient *voiceClient,
                           QWidget *parent)
    : QWidget(parent)
    , m_nav(nav)
    , m_data(data)
    , m_chat(chat)
    , m_voiceClient(voiceClient)
{
    auto *root = new QVBoxLayout(this);
    root->setContentsMargins(24, 16, 24, 24);
    root->setSpacing(0);

    m_card = new QFrame(this);
    m_card->setObjectName(QStringLiteral("PageCard"));
    m_card->setSizePolicy(QSizePolicy::Expanding, QSizePolicy::Expanding);
    auto *cardLayout = new QVBoxLayout(m_card);
    cardLayout->setContentsMargins(56, 28, 56, 36);
    cardLayout->setSpacing(0);

    auto *assistantSection = new QWidget(m_card);
    auto *assistantLayout = new QVBoxLayout(assistantSection);
    assistantLayout->setContentsMargins(0, 0, 0, 0);
    assistantLayout->setSpacing(10);

    auto *mascot = new QLabel(assistantSection);
    mascot->setObjectName(QStringLiteral("Mascot"));
    mascot->setPixmap(QPixmap(QStringLiteral(":/assets/mascot.png")).scaled(245, 245, Qt::KeepAspectRatio, Qt::SmoothTransformation));
    mascot->setAlignment(Qt::AlignCenter);

    auto *shadow = new MascotShadowWidget(assistantSection);
    shadow->setObjectName(QStringLiteral("MascotShadow"));

    auto *hello = new QLabel(QStringLiteral("您好！我能为您做些什么？"), assistantSection);
    hello->setObjectName(QStringLiteral("HeroTitle"));
    hello->setAlignment(Qt::AlignCenter);
    hello->setWordWrap(true);

    assistantLayout->addStretch(1);
    assistantLayout->addWidget(mascot, 0, Qt::AlignCenter);
    assistantLayout->addWidget(shadow, 0, Qt::AlignCenter);
    assistantLayout->addSpacing(14);
    assistantLayout->addWidget(hello);
    assistantLayout->addStretch(1);

    auto *voiceSection = new QWidget(m_card);
    auto *voiceLayout = new QVBoxLayout(voiceSection);
    voiceLayout->setContentsMargins(0, 0, 0, 0);
    voiceLayout->setSpacing(12);
    m_voiceButton = new VoiceButton(voiceSection);
    m_voiceButton->setFixedSize(158, 158);
    m_voiceHint = new QLabel(QStringLiteral("点击说话"), voiceSection);
    m_voiceHint->setObjectName(QStringLiteral("VoiceHintLabel"));
    m_voiceHint->setAlignment(Qt::AlignCenter);
    m_voiceHint->setWordWrap(true);
    m_voiceHint->setMinimumHeight(44);
    m_voiceHint->setMaximumWidth(820);
    m_voiceHint->setSizePolicy(QSizePolicy::Preferred, QSizePolicy::Minimum);
    voiceLayout->addStretch(1);
    voiceLayout->addWidget(m_voiceButton, 0, Qt::AlignCenter);
    voiceLayout->addWidget(m_voiceHint, 0, Qt::AlignHCenter);
    voiceLayout->addStretch(1);

    cardLayout->addStretch(8);
    cardLayout->addWidget(assistantSection, 48);
    cardLayout->addWidget(voiceSection, 28);
    cardLayout->addStretch(16);

    root->addWidget(m_card, 1);

    connect(m_voiceButton, &QPushButton::clicked, m_voiceClient, &VoiceClient::toggleRecording);
    connect(m_voiceClient, &VoiceClient::recordingChanged, this, [this](bool recording) {
        m_voiceButton->setState(recording ? VoiceState::Listening : VoiceState::Processing);
        m_voiceHint->setText(recording ? QStringLiteral("请说话，自动识别中") : QStringLiteral("正在识别..."));
        if (!recording && m_nav->currentPage() == PageType::ChatHome) {
            m_nav->setProperty("lastQuestion", QStringLiteral("正在识别..."));
            m_nav->setProperty("lastAnswer", QStringLiteral("正在识别，请稍候..."));
            m_nav->setProperty("lastUsesMap", false);
            m_nav->setProperty("lastShowsRoute", false);
            m_nav->setProperty("lastRouteReady", false);
            m_nav->goToPage(PageType::ChatReply);
        }
    });
    connect(m_voiceClient, &VoiceClient::stateChanged, this, [this](const QString &state) {
        if (state == QStringLiteral("idle")) {
            m_voiceButton->setState(VoiceState::Idle);
            m_voiceHint->setText(QStringLiteral("点击说话"));
        } else if (state == QStringLiteral("user_speaking")) {
            m_voiceButton->setState(VoiceState::Listening);
            m_voiceHint->setText(QStringLiteral("正在聆听"));
        } else if (state == QStringLiteral("ai_speaking")) {
            m_voiceButton->setState(VoiceState::Processing);
            m_voiceHint->setText(QStringLiteral("正在播报，可直接说话打断"));
        } else if (state == QStringLiteral("user_interrupting")) {
            m_voiceButton->setState(VoiceState::Listening);
            m_voiceHint->setText(QStringLiteral("已打断，请继续说"));
        } else {
            m_voiceButton->setState(VoiceState::Processing);
            m_voiceHint->setText(QStringLiteral("正在思考"));
            if (state == QStringLiteral("ai_thinking") && m_nav->currentPage() == PageType::ChatHome) {
                m_nav->setProperty("lastQuestion", QStringLiteral("正在识别..."));
                m_nav->setProperty("lastAnswer", QStringLiteral("正在思考..."));
                m_nav->setProperty("lastUsesMap", false);
                m_nav->setProperty("lastShowsRoute", false);
                m_nav->setProperty("lastRouteReady", false);
                m_nav->goToPage(PageType::ChatReply);
            }
        }
    });
    connect(m_voiceClient, &VoiceClient::asrPartial, this, [this](const QString &text) {
        m_voiceHint->setText(QStringLiteral("正在识别：%1").arg(text));
    });
    connect(m_voiceClient, &VoiceClient::asrFinal, this, [this](const QString &text) {
        m_voiceHint->setText(QStringLiteral("识别：%1").arg(text));
        m_nav->setProperty("lastQuestion", text);
        m_nav->setProperty("lastAnswer", QStringLiteral("正在思考..."));
        m_nav->setProperty("lastUsesMap", false);
        m_nav->setProperty("lastShowsRoute", false);
        m_nav->setProperty("lastRouteReady", false);
        m_nav->goToPage(PageType::ChatReply);
    });
    connect(m_voiceClient, &VoiceClient::mapToolCall, this, [this](const QString &query, const QString &) {
        if (m_nav->currentPage() != PageType::ChatHome) {
            return;
        }
        const ChatReply local = m_chat->replyForText(query);
        if (local.showsRoute) {
            m_nav->setCurrentPOI(local.poi);
        }
        m_nav->setProperty("lastQuestion", query);
        m_nav->setProperty("lastAnswer", local.assistantText);
        m_nav->setProperty("lastUsesMap", local.usesMap);
        m_nav->setProperty("lastShowsRoute", local.showsRoute);
        m_nav->setProperty("lastRouteReady", false);
        m_voiceClient->speakToolResult(local.assistantText);
        m_nav->goToPage(PageType::ChatReply);
    });
    connect(m_voiceClient, &VoiceClient::errorText, this, [this](const QString &message) {
        m_voiceButton->setState(VoiceState::Idle);
        m_voiceHint->setText(QStringLiteral("语音错误：%1").arg(message));
    });
}
