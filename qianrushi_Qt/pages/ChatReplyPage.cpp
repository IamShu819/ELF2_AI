// 对话回复页面实现 - 左侧聊天，右侧展示 AI 查询出的本地地图路线
#include "ChatReplyPage.h"

#include <QFrame>
#include <QHBoxLayout>
#include <QLabel>
#include <QPixmap>
#include <QPushButton>
#include <QSizePolicy>
#include <QVBoxLayout>

#include "core/MockChatService.h"
#include "core/MockDataService.h"
#include "core/NavigationController.h"
#include "core/VoiceClient.h"
#include "widgets/MapWidget.h"
#include "widgets/VoiceButton.h"

ChatReplyPage::ChatReplyPage(NavigationController *nav,
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
    auto *root = new QHBoxLayout(this);
    root->setContentsMargins(28, 16, 28, 16);
    root->setSpacing(20);

    auto *chatCard = new QFrame(this);
    chatCard->setObjectName(QStringLiteral("PageCard"));
    auto *chatLayout = new QVBoxLayout(chatCard);
    chatLayout->setContentsMargins(26, 22, 26, 22);
    chatLayout->setSpacing(14);

    auto *back = new QPushButton(QStringLiteral("← 返回"), chatCard);
    back->setObjectName(QStringLiteral("SecondaryButton"));
    back->setCursor(Qt::PointingHandCursor);
    chatLayout->addWidget(back, 0, Qt::AlignLeft);

    m_userBubble = new QLabel(chatCard);
    m_userBubble->setObjectName(QStringLiteral("UserBubble"));
    m_userBubble->setWordWrap(true);
    m_userBubble->setMinimumHeight(54);
    m_userBubble->setMaximumWidth(560);
    m_userBubble->setSizePolicy(QSizePolicy::Preferred, QSizePolicy::Minimum);
    chatLayout->addWidget(m_userBubble, 0, Qt::AlignRight);

    auto *aiRow = new QWidget(chatCard);
    auto *aiRowLayout = new QHBoxLayout(aiRow);
    aiRowLayout->setContentsMargins(0, 0, 0, 0);
    aiRowLayout->setSpacing(12);

    auto *assistantAvatar = new QLabel(aiRow);
    assistantAvatar->setObjectName(QStringLiteral("AssistantAvatar"));
    assistantAvatar->setFixedSize(54, 54);
    assistantAvatar->setAlignment(Qt::AlignCenter);
    assistantAvatar->setPixmap(QPixmap(QStringLiteral(":/assets/mascot.png")).scaled(46, 46, Qt::KeepAspectRatio, Qt::SmoothTransformation));

    auto *aiCard = new QFrame(chatCard);
    aiCard->setObjectName(QStringLiteral("AssistantCard"));
    auto *aiLayout = new QVBoxLayout(aiCard);
    aiLayout->setContentsMargins(20, 18, 20, 18);
    aiLayout->setSpacing(10);
    m_answer = new QLabel(aiCard);
    m_answer->setObjectName(QStringLiteral("BodyTextLarge"));
    m_answer->setWordWrap(true);
    m_playbackStatus = new QLabel(QStringLiteral("正在语音播报..."), aiCard);
    m_playbackStatus->setObjectName(QStringLiteral("MetaText"));
    m_playbackStatus->hide();
    m_meta = new QLabel(aiCard);
    m_meta->setObjectName(QStringLiteral("MetaText"));
    m_meta->setWordWrap(true);
    aiLayout->addWidget(m_answer);
    aiLayout->addWidget(m_playbackStatus);
    aiLayout->addWidget(m_meta);
    aiRowLayout->addWidget(assistantAvatar, 0, Qt::AlignTop);
    aiRowLayout->addWidget(aiCard, 1);
    chatLayout->addWidget(aiRow);

    chatLayout->addStretch(1);

    auto *voiceRow = new QHBoxLayout;
    m_voiceStatus = new QLabel(QStringLiteral("继续直接问我地点或路线"), chatCard);
    m_voiceStatus->setObjectName(QStringLiteral("InputBar"));
    m_voiceStatus->setWordWrap(true);
    m_voiceStatus->setMinimumHeight(58);
    m_voiceStatus->setSizePolicy(QSizePolicy::Expanding, QSizePolicy::Minimum);
    m_voiceButton = new VoiceButton(chatCard);
    m_voiceButton->setFixedSize(76, 76);
    voiceRow->addWidget(m_voiceStatus, 1, Qt::AlignVCenter);
    voiceRow->addWidget(m_voiceButton, 0, Qt::AlignBottom);
    chatLayout->addLayout(voiceRow);

    m_routeCard = new QFrame(this);
    m_routeCard->setObjectName(QStringLiteral("PageCard"));
    auto *routeLayout = new QVBoxLayout(m_routeCard);
    routeLayout->setContentsMargins(22, 22, 22, 22);
    routeLayout->setSpacing(12);

    m_routeTitle = new QLabel(QStringLiteral("AI 地图路线"), m_routeCard);
    m_routeTitle->setObjectName(QStringLiteral("SectionTitle"));
    m_routeSummary = new QLabel(m_routeCard);
    m_routeSummary->setObjectName(QStringLiteral("BodyTextLarge"));
    m_routeSummary->setWordWrap(true);
    m_routeFeature = new QLabel(m_routeCard);
    m_routeFeature->setObjectName(QStringLiteral("MetaText"));
    m_routeFeature->setWordWrap(true);
    m_routeMap = new MapWidget(m_routeCard);
    m_routeMap->setMinimumHeight(420);

    m_routeDetailButton = new QPushButton(QStringLiteral("查看详细步骤"), m_routeCard);
    m_routeDetailButton->setObjectName(QStringLiteral("PrimaryButton"));
    m_routeDetailButton->setCursor(Qt::PointingHandCursor);

    routeLayout->addWidget(m_routeTitle);
    routeLayout->addWidget(m_routeSummary);
    routeLayout->addWidget(m_routeFeature);
    routeLayout->addWidget(m_routeMap, 1);
    routeLayout->addWidget(m_routeDetailButton, 0, Qt::AlignLeft);
    m_routeCard->hide();

    root->addWidget(chatCard, 11);
    root->addWidget(m_routeCard, 10);

    connect(back, &QPushButton::clicked, m_nav, [this]() { m_nav->goToPage(PageType::ChatHome); });
    connect(m_routeDetailButton, &QPushButton::clicked, m_nav, [this]() { m_nav->goToPage(PageType::RoutePlan); });
    connect(m_voiceButton, &QPushButton::clicked, m_voiceClient, &VoiceClient::toggleRecording);
    connect(m_voiceClient, &VoiceClient::recordingChanged, this, [this](bool recording) {
        m_voiceButton->setState(recording ? VoiceState::Listening : VoiceState::Processing);
        m_voiceStatus->setText(recording ? QStringLiteral("请说话，自动识别中") : QStringLiteral("正在识别..."));
    });
    connect(m_voiceClient, &VoiceClient::stateChanged, this, [this](const QString &state) {
        if (state == QStringLiteral("idle")) {
            m_voiceButton->setState(VoiceState::Idle);
            m_voiceStatus->setText(QStringLiteral("继续直接问我地点或路线"));
        } else if (state == QStringLiteral("user_speaking")) {
            m_voiceButton->setState(VoiceState::Listening);
            m_voiceStatus->setText(QStringLiteral("正在聆听"));
        } else if (state == QStringLiteral("ai_speaking")) {
            m_voiceButton->setState(VoiceState::Processing);
            m_voiceStatus->setText(QStringLiteral("正在播报，可直接说话打断"));
        } else if (state == QStringLiteral("user_interrupting")) {
            m_voiceButton->setState(VoiceState::Listening);
            m_voiceStatus->setText(QStringLiteral("已打断，请继续说"));
        } else {
            m_voiceButton->setState(VoiceState::Processing);
            m_voiceStatus->setText(QStringLiteral("正在思考"));
        }
    });
    connect(m_voiceClient, &VoiceClient::asrPartial, this, [this](const QString &text) {
        m_voiceStatus->setText(QStringLiteral("正在识别：%1").arg(text));
        if (m_pendingVoiceQuestion.isEmpty()) {
            m_userBubble->setText(QStringLiteral("正在识别：%1").arg(text));
        }
    });
    connect(m_voiceClient, &VoiceClient::asrFinal, this, [this](const QString &text) {
        m_pendingVoiceQuestion = text;
        m_userBubble->setText(text);
        m_answer->setText(QStringLiteral("正在思考..."));
        m_voiceStatus->setText(QStringLiteral("识别：%1").arg(text));

        m_nav->setProperty("lastQuestion", text);
        m_nav->setProperty("lastUsesMap", false);
        m_nav->setProperty("lastShowsRoute", false);
        m_nav->setProperty("lastRouteReady", false);
        updateRoutePreview();
    });
    connect(m_voiceClient, &VoiceClient::streamingStarted, this, [this]() {
        if (m_answer->text() == QStringLiteral("正在思考...")) {
            m_answer->clear();
        }
    });
    connect(m_voiceClient, &VoiceClient::replyPartial, this, [this](const QString &answer) {
        if (!answer.trimmed().isEmpty()) {
            m_answer->setText(answer);
        }
    });
    connect(m_voiceClient, &VoiceClient::streamingFinished, this, [this](const QString &answer) {
        applyVoiceReply(answer);
    });
    connect(m_voiceClient, &VoiceClient::mapToolCall, this, [this](const QString &query, const QString &) {
        if (m_nav->currentPage() != PageType::ChatReply) {
            return;
        }
        applyMapToolResult(query);
    });
    connect(m_data, &MockDataService::routeToPOIReady, this, [this](int requestId, const POIInfo &poi, const RouteInfo &route) {
        if (requestId != m_pendingRouteRequestId) {
            return;
        }
        applyRouteResult(poi, route);
    });
    connect(m_voiceClient, &VoiceClient::audioPlayingChanged, this, [this](bool playing) {
        m_playbackStatus->setText(QStringLiteral("正在语音播报..."));
        m_playbackStatus->setVisible(playing);
    });
    connect(m_voiceClient, &VoiceClient::ttsError, this, [this](const QString &message) {
        m_playbackStatus->setText(QStringLiteral("语音合成失败：%1").arg(message));
        m_playbackStatus->show();
    });
    connect(m_voiceClient, &VoiceClient::errorText, this, [this](const QString &message) {
        m_voiceButton->setState(VoiceState::Idle);
        m_voiceStatus->setText(QStringLiteral("语音错误：%1").arg(message));
    });
}

void ChatReplyPage::refresh()
{
    if (!m_pendingVoiceQuestion.trimmed().isEmpty()) {
        updateRoutePreview();
        return;
    }

    const QString question = m_nav->property("lastQuestion").toString();
    const QString answer = m_nav->property("lastAnswer").toString();
    m_userBubble->setText(question.isEmpty() ? QStringLiteral("请直接问我地点或路线") : question);
    m_answer->setText(answer.isEmpty() ? QStringLiteral("正在思考...") : answer);
    m_meta->clear();
    if (m_nav->property("lastShowsRoute").toBool() && m_nav->hasCurrentPOI()) {
        const POIInfo poi = m_nav->currentPOI();
        m_meta->setText(QStringLiteral("目的地：%1 · 约 %2 米 · %3 分钟").arg(poi.name).arg(poi.distanceMeters).arg(poi.durationMinutes));
        if (!m_nav->property("lastRouteReady").toBool() && m_pendingRouteRequestId == 0) {
            prepareRoutePreview(poi);
        }
    }
    updateRoutePreview();
}

void ChatReplyPage::ask(const QString &text)
{
    const ChatReply reply = m_chat->replyForText(text);
    if (reply.showsRoute) {
        prepareRoutePreview(reply.poi);
    } else {
        m_pendingRouteRequestId = 0;
        m_nav->setProperty("lastRouteReady", false);
    }
    m_nav->setProperty("lastQuestion", reply.userText);
    m_nav->setProperty("lastAnswer", reply.assistantText);
    m_nav->setProperty("lastUsesMap", reply.usesMap);
    m_nav->setProperty("lastShowsRoute", reply.showsRoute);
    refresh();
}

void ChatReplyPage::applyVoiceReply(const QString &answer)
{
    const QString question = m_pendingVoiceQuestion.trimmed();
    if (question.isEmpty()) {
        return;
    }

    const ChatReply local = m_chat->replyForText(question);
    const QString finalAnswer = local.usesMap
        ? local.assistantText
        : answer.trimmed();
    if (!local.usesMap && finalAnswer.isEmpty()) {
        m_answer->setText(QStringLiteral("未收到模型回复，请检查后端模型连接。"));
        m_voiceStatus->setText(QStringLiteral("模型暂无回复，请查看后端日志"));
        return;
    }
    if (local.showsRoute) {
        prepareRoutePreview(local.poi);
    } else {
        m_pendingRouteRequestId = 0;
        m_nav->setProperty("lastRouteReady", false);
    }
    m_nav->setProperty("lastQuestion", question);
    m_nav->setProperty("lastAnswer", finalAnswer);
    m_nav->setProperty("lastUsesMap", local.usesMap);
    m_nav->setProperty("lastShowsRoute", local.showsRoute);
    m_answer->setText(finalAnswer);
    if (local.showsRoute) {
        m_meta->setText(QStringLiteral("目的地：%1 · 约 %2 米 · %3 分钟").arg(local.poi.name).arg(local.poi.distanceMeters).arg(local.poi.durationMinutes));
    } else {
        m_meta->clear();
    }
    m_voiceStatus->setText(QStringLiteral("继续直接问我地点或路线"));
    m_pendingVoiceQuestion.clear();
    updateRoutePreview();
}

void ChatReplyPage::applyMapToolResult(const QString &query)
{
    const ChatReply local = m_chat->replyForText(query);
    if (!local.usesMap) {
        return;
    }

    if (local.showsRoute) {
        prepareRoutePreview(local.poi);
        m_meta->setText(QStringLiteral("目的地：%1 · 约 %2 米 · %3 分钟").arg(local.poi.name).arg(local.poi.distanceMeters).arg(local.poi.durationMinutes));
    } else {
        m_pendingRouteRequestId = 0;
        m_nav->setProperty("lastRouteReady", false);
        m_meta->clear();
    }

    m_nav->setProperty("lastQuestion", query);
    m_nav->setProperty("lastAnswer", local.assistantText);
    m_nav->setProperty("lastUsesMap", local.usesMap);
    m_nav->setProperty("lastShowsRoute", local.showsRoute);
    m_userBubble->setText(query);
    m_answer->setText(local.assistantText);
    m_pendingVoiceQuestion.clear();
    updateRoutePreview();
    m_voiceClient->speakToolResult(local.assistantText);
}

void ChatReplyPage::prepareRoutePreview(const POIInfo &poi)
{
    m_nav->setCurrentPOI(poi);
    m_nav->setProperty("lastRouteReady", false);
    m_pendingRouteRequestId = m_data->requestRouteToPOI(poi);
}

void ChatReplyPage::applyRouteResult(const POIInfo &poi, const RouteInfo &route)
{
    m_pendingRouteRequestId = 0;
    m_nav->setCurrentPOI(poi);
    m_nav->setCurrentRoute(route);
    m_nav->setProperty("lastRouteReady", true);
    if (m_nav->currentPage() == PageType::ChatReply) {
        m_meta->setText(QStringLiteral("目的地：%1 · 约 %2 米 · %3 分钟").arg(poi.name).arg(route.totalDistanceMeters).arg(route.totalDurationMinutes));
        updateRoutePreview();
    }
}

void ChatReplyPage::updateRoutePreview()
{
    const bool usesMap = m_nav->property("lastUsesMap").toBool();
    const bool showsRoute = m_nav->property("lastShowsRoute").toBool();
    if (!usesMap) {
        m_routeCard->hide();
        m_routeTitle->setText(QStringLiteral("AI 地图路线"));
        m_routeSummary->setText(QStringLiteral("暂无路线"));
        m_routeFeature->clear();
        m_routeMap->setPOIs({});
        m_routeMap->clearRoute();
        m_routeDetailButton->hide();
        return;
    }

    m_routeCard->show();
    if (!showsRoute) {
        m_routeTitle->setText(QStringLiteral("当前位置"));
        m_routeSummary->setText(QStringLiteral("本终端位置"));
        m_routeFeature->setText(QStringLiteral("右侧已显示设备当前位置"));
        m_routeMap->setPOIs({});
        m_routeMap->clearRoute();
        m_routeDetailButton->hide();
        return;
    }

    if (!m_nav->hasCurrentPOI()) {
        m_routeCard->hide();
        return;
    }

    const POIInfo poi = m_nav->currentPOI();
    m_routeTitle->setText(poi.name);
    m_routeMap->setPOIs({poi});
    m_routeMap->setSelectedPOI(poi.id);
    if (!m_nav->property("lastRouteReady").toBool()) {
        m_routeSummary->setText(QStringLiteral("正在规划步行路线..."));
        m_routeFeature->setText(poi.description);
        m_routeMap->clearRoute();
        m_routeDetailButton->hide();
        return;
    }

    const RouteInfo route = m_nav->currentRoute();
    m_routeSummary->setText(QStringLiteral("约 %1 米 · 步行 %2 分钟").arg(route.totalDistanceMeters).arg(route.totalDurationMinutes));
    m_routeFeature->setText(route.feature.isEmpty() ? poi.description : route.feature);
    if (route.pathPoints.size() >= 2) {
        m_routeMap->setRoute(route);
    } else {
        m_routeMap->clearRoute();
    }
    m_routeDetailButton->show();
}
