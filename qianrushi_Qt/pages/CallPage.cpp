#include "CallPage.h"

#include <QApplication>
#include <QFrame>
#include <QFontMetrics>
#include <QHBoxLayout>
#include <QLabel>
#include <QPushButton>
#include <QResizeEvent>
#include <QStyle>
#include <QTimer>
#include <QVBoxLayout>

namespace {
QString mmss(qint64 seconds)
{
    const qint64 minutes = seconds / 60;
    const qint64 remain = seconds % 60;
    return QStringLiteral("%1:%2")
        .arg(minutes, 2, 10, QLatin1Char('0'))
        .arg(remain, 2, 10, QLatin1Char('0'));
}
}

CallPage::CallPage(CallClient *callClient, QWidget *parent)
    : QWidget(parent)
    , m_callClient(callClient)
    , m_callTimer(new QTimer(this))
    , m_waveformTimer(new QTimer(this))
{
    setObjectName(QStringLiteral("CallPage"));
    buildUi();

    m_callTimer->setInterval(1000);
    m_waveformTimer->setInterval(260);
    connect(m_callTimer, &QTimer::timeout, this, &CallPage::updateElapsedTime);
    connect(m_waveformTimer, &QTimer::timeout, this, &CallPage::updateWaveform);

    if (m_callClient) {
        m_state = m_callClient->state();
        m_micMuted = m_callClient->isMicMuted();
        m_speakerMuted = m_callClient->isSpeakerMuted();
        connect(m_callClient, &CallClient::stateChanged, this, &CallPage::updateCallState);
        connect(m_callClient, &CallClient::statusTextChanged, this, &CallPage::updateStatusText);
        connect(m_callClient, &CallClient::micMutedChanged, this, &CallPage::updateMicMuted);
        connect(m_callClient, &CallClient::speakerMutedChanged, this, &CallPage::updateSpeakerMuted);
        connect(m_callClient, &CallClient::errorText, this, &CallPage::showCallError);
        connect(m_callButton, &QPushButton::clicked, this, [this]() {
            if (!m_callClient) {
                return;
            }
            m_lastError.clear();
            m_errorActive = false;
            if (m_state == CallClient::State::Connecting) {
                m_callClient->hangup();
            } else {
                m_callClient->startCall();
            }
        });
        connect(m_micButton, &QPushButton::clicked, m_callClient, &CallClient::toggleMicMuted);
        connect(m_speakerButton, &QPushButton::clicked, m_callClient, &CallClient::toggleSpeakerMuted);
        connect(m_hangupButton, &QPushButton::clicked, m_callClient, &CallClient::hangup);
    }

    applyState();
}

void CallPage::buildUi()
{
    auto *root = new QVBoxLayout(this);
    root->setContentsMargins(20, 16, 20, 16);
    root->setSpacing(14);

    auto *header = new QWidget(this);
    header->setObjectName(QStringLiteral("CallHeader"));
    auto *headerLayout = new QHBoxLayout(header);
    headerLayout->setContentsMargins(0, 0, 0, 0);
    headerLayout->setSpacing(12);

    auto *titleBox = new QVBoxLayout;
    titleBox->setContentsMargins(0, 0, 0, 0);
    titleBox->setSpacing(4);
    m_title = new QLabel(QStringLiteral("通话服务"), header);
    m_title->setObjectName(QStringLiteral("CallTitle"));
    m_subtitle = new QLabel(QStringLiteral("与云端工作人员建立实时语音通话"), header);
    m_subtitle->setObjectName(QStringLiteral("CallSubtitle"));
    titleBox->addWidget(m_title);
    titleBox->addWidget(m_subtitle);

    m_statusBadge = new QLabel(header);
    m_statusBadge->setObjectName(QStringLiteral("CallStatusBadge"));
    m_statusBadge->setAlignment(Qt::AlignCenter);
    m_statusBadge->setMinimumWidth(84);

    headerLayout->addLayout(titleBox, 1);
    headerLayout->addWidget(m_statusBadge, 0, Qt::AlignTop | Qt::AlignRight);

    m_panel = new QFrame(this);
    m_panel->setObjectName(QStringLiteral("CallPanel"));
    auto *panelLayout = new QVBoxLayout(m_panel);
    panelLayout->setContentsMargins(28, 22, 28, 22);
    panelLayout->setSpacing(12);

    auto *statusRow = new QHBoxLayout;
    statusRow->setContentsMargins(0, 0, 0, 0);
    statusRow->setSpacing(10);
    m_statusIcon = new QLabel(m_panel);
    m_statusIcon->setObjectName(QStringLiteral("CallStatusIcon"));
    m_statusIcon->setAlignment(Qt::AlignCenter);
    m_statusText = new QLabel(m_panel);
    m_statusText->setObjectName(QStringLiteral("CallStatusText"));
    m_statusText->setAlignment(Qt::AlignLeft | Qt::AlignVCenter);
    m_statusText->setWordWrap(false);
    statusRow->addStretch(1);
    statusRow->addWidget(m_statusIcon);
    statusRow->addWidget(m_statusText);
    statusRow->addStretch(1);

    m_urlText = new QLabel(m_panel);
    m_urlText->setObjectName(QStringLiteral("CallUrlText"));
    m_urlText->setAlignment(Qt::AlignCenter);
    m_urlText->setWordWrap(false);

    auto *waveRow = new QHBoxLayout;
    waveRow->setContentsMargins(0, 0, 0, 0);
    waveRow->setSpacing(16);
    m_waveformLabel = new QLabel(QStringLiteral("-------"), m_panel);
    m_waveformLabel->setObjectName(QStringLiteral("CallWaveform"));
    m_waveformLabel->setAlignment(Qt::AlignRight | Qt::AlignVCenter);
    m_timerText = new QLabel(QStringLiteral("00:00"), m_panel);
    m_timerText->setObjectName(QStringLiteral("CallTimerText"));
    m_timerText->setAlignment(Qt::AlignLeft | Qt::AlignVCenter);
    waveRow->addStretch(1);
    waveRow->addWidget(m_waveformLabel);
    waveRow->addWidget(m_timerText);
    waveRow->addStretch(1);

    auto *primaryRow = new QHBoxLayout;
    primaryRow->setContentsMargins(0, 0, 0, 0);
    m_callButton = new QPushButton(QStringLiteral("呼叫工作人员"), m_panel);
    m_callButton->setObjectName(QStringLiteral("CallPrimaryButton"));
    m_callButton->setCursor(Qt::PointingHandCursor);
    primaryRow->addStretch(1);
    primaryRow->addWidget(m_callButton, 0);
    primaryRow->addStretch(1);

    auto *controlsRow = new QHBoxLayout;
    controlsRow->setContentsMargins(0, 0, 0, 0);
    controlsRow->setSpacing(12);
    m_micButton = new QPushButton(QStringLiteral("麦克风开"), m_panel);
    m_micButton->setObjectName(QStringLiteral("CallToggleButton"));
    m_micButton->setCursor(Qt::PointingHandCursor);
    m_speakerButton = new QPushButton(QStringLiteral("扬声器关"), m_panel);
    m_speakerButton->setObjectName(QStringLiteral("CallToggleButton"));
    m_speakerButton->setCursor(Qt::PointingHandCursor);
    m_hangupButton = new QPushButton(QStringLiteral("挂断"), m_panel);
    m_hangupButton->setObjectName(QStringLiteral("CallDangerButton"));
    m_hangupButton->setCursor(Qt::PointingHandCursor);
    controlsRow->addStretch(1);
    controlsRow->addWidget(m_micButton);
    controlsRow->addWidget(m_speakerButton);
    controlsRow->addWidget(m_hangupButton);
    controlsRow->addStretch(1);

    m_hintText = new QLabel(m_panel);
    m_hintText->setObjectName(QStringLiteral("CallHint"));
    m_hintText->setAlignment(Qt::AlignCenter);
    m_hintText->setWordWrap(false);

    panelLayout->addLayout(statusRow);
    panelLayout->addWidget(m_urlText);
    panelLayout->addLayout(waveRow);
    panelLayout->addLayout(primaryRow);
    panelLayout->addLayout(controlsRow);
    panelLayout->addWidget(m_hintText);

    m_lastEventText = new QLabel(QStringLiteral("最近状态：等待用户发起通话"), this);
    m_lastEventText->setObjectName(QStringLiteral("CallLastEvent"));
    m_lastEventText->setWordWrap(false);

    root->addWidget(header, 0);
    root->addWidget(m_panel, 1);
    root->addWidget(m_lastEventText, 0);
}

void CallPage::updateCallState(CallClient::State state)
{
    m_state = state;
    if (state != CallClient::State::Disconnected) {
        m_errorActive = false;
        m_lastError.clear();
    }
    if (state == CallClient::State::InCall) {
        m_elapsed.restart();
        m_callTimer->start();
        m_waveformTimer->start();
    } else {
        m_callTimer->stop();
        m_waveformTimer->stop();
        m_waveformFrame = 0;
    }
    applyState();
}

void CallPage::updateStatusText(const QString &)
{
    applyState();
}

void CallPage::updateMicMuted(bool muted)
{
    m_micMuted = muted;
    updateButtonTexts();
}

void CallPage::updateSpeakerMuted(bool muted)
{
    m_speakerMuted = muted;
    updateButtonTexts();
}

void CallPage::showCallError(const QString &message)
{
    m_errorActive = true;
    m_lastError = message.trimmed();
    if (m_lastError.isEmpty()) {
        m_lastError = QStringLiteral("连接失败");
    }
    m_lastEventText->setText(QStringLiteral("最近状态：%1").arg(m_lastError));
    applyState();
}

void CallPage::updateElapsedTime()
{
    if (m_state == CallClient::State::InCall) {
        m_timerText->setText(elapsedText());
    }
}

void CallPage::updateWaveform()
{
    static const char *frames[] = {
        "▂ ▄ ▆ █ ▆ ▄ ▂",
        "▄ ▆ █ ▆ ▄ ▂ ▄",
        "▆ █ ▆ ▄ ▂ ▄ ▆"
    };
    m_waveformLabel->setText(QString::fromUtf8(frames[m_waveformFrame % 3]));
    ++m_waveformFrame;
}

void CallPage::resizeEvent(QResizeEvent *event)
{
    QWidget::resizeEvent(event);
    const bool compact = isCompact();
    auto *rootLayout = qobject_cast<QVBoxLayout *>(layout());
    if (rootLayout) {
        rootLayout->setContentsMargins(compact ? 12 : 20, compact ? 10 : 16, compact ? 12 : 20, compact ? 10 : 16);
        rootLayout->setSpacing(compact ? 8 : 14);
    }
    if (m_panel && m_panel->layout()) {
        m_panel->layout()->setContentsMargins(compact ? 14 : 28, compact ? 12 : 22, compact ? 14 : 28, compact ? 12 : 22);
    }
    m_subtitle->setVisible(!compact);
    updateButtonTexts();
    updateUrlText();
}

void CallPage::applyState()
{
    QString badgeText;
    QString badgeStatus;
    QString statusText;
    QString hintText;
    QString lastEvent;
    QString icon;

    if (m_errorActive) {
        badgeText = QStringLiteral("错误");
        badgeStatus = QStringLiteral("error");
        statusText = QStringLiteral("连接失败");
        hintText = m_lastError;
        lastEvent = m_lastError;
        icon = QStringLiteral("×");
        m_waveformLabel->setText(QStringLiteral("-------"));
        m_timerText->setText(QStringLiteral("00:00"));
    } else if (m_state == CallClient::State::Connecting) {
        badgeText = QStringLiteral("◌ 连接中");
        badgeStatus = QStringLiteral("connecting");
        statusText = QStringLiteral("等待工作人员接入");
        hintText = QStringLiteral("连接中，请等待工作人员接入");
        lastEvent = QStringLiteral("通话服务连接中");
        icon = QStringLiteral("◌");
        m_waveformLabel->setText(QStringLiteral("正在连接..."));
        m_timerText->setText(QStringLiteral("00:00"));
    } else if (m_state == CallClient::State::InCall) {
        badgeText = QStringLiteral("● 通话中");
        badgeStatus = QStringLiteral("connected");
        statusText = QStringLiteral("通话中");
        hintText = isCompact() ? QStringLiteral("半双工：开扬声器会自动关麦克风")
                               : QStringLiteral("半双工模式：打开扬声器会自动关闭麦克风");
        lastEvent = isCompact() ? QStringLiteral("语音问答已暂停")
                                : QStringLiteral("语音问答已暂停，挂断后自动恢复");
        icon = QStringLiteral("●");
        if (!m_waveformTimer->isActive()) {
            m_waveformTimer->start();
        }
        m_timerText->setText(elapsedText());
    } else {
        badgeText = QStringLiteral("○ 未通话");
        badgeStatus = QStringLiteral("disconnected");
        statusText = QStringLiteral("当前未通话");
        hintText = QStringLiteral("等待用户发起通话");
        lastEvent = QStringLiteral("等待用户发起通话");
        icon = QStringLiteral("○");
        m_waveformLabel->setText(QStringLiteral("-------"));
        m_timerText->setText(QStringLiteral("00:00"));
    }

    m_statusBadge->setText(badgeText);
    m_statusBadge->setProperty("status", badgeStatus);
    m_statusIcon->setText(icon);
    m_statusText->setText(statusText);
    m_hintText->setText(hintText);
    m_lastEventText->setText(QStringLiteral("最近状态：%1").arg(lastEvent));
    refreshWidgetStyle(m_statusBadge);
    updateUrlText();
    updateButtonTexts();
}

void CallPage::updateButtonTexts()
{
    const bool compact = isCompact();
    if (m_errorActive) {
        m_callButton->setText(compact ? QStringLiteral("呼叫") : QStringLiteral("重新呼叫"));
    } else if (m_state == CallClient::State::Connecting) {
        m_callButton->setText(compact ? QStringLiteral("取消") : QStringLiteral("取消连接"));
    } else {
        m_callButton->setText(compact ? QStringLiteral("呼叫") : QStringLiteral("呼叫工作人员"));
    }
    m_micButton->setText(compact
        ? (m_micMuted ? QStringLiteral("麦关") : QStringLiteral("麦开"))
        : (m_micMuted ? QStringLiteral("麦克风关") : QStringLiteral("麦克风开")));
    m_speakerButton->setText(compact
        ? (m_speakerMuted ? QStringLiteral("扬关") : QStringLiteral("扬开"))
        : (m_speakerMuted ? QStringLiteral("扬声器关") : QStringLiteral("扬声器开")));
    m_hangupButton->setText(QStringLiteral("挂断"));

    m_callButton->setVisible(m_state != CallClient::State::InCall || m_errorActive);
    m_hangupButton->setVisible(m_state == CallClient::State::InCall && !m_errorActive);
    m_micButton->setVisible(true);
    m_speakerButton->setVisible(true);

    m_micButton->setProperty("active", !m_micMuted);
    m_speakerButton->setProperty("active", !m_speakerMuted);
    m_callButton->setProperty("active", true);
    m_callButton->setProperty("danger", m_state == CallClient::State::Connecting);
    m_hangupButton->setProperty("danger", true);
    refreshWidgetStyle(m_micButton);
    refreshWidgetStyle(m_speakerButton);
    refreshWidgetStyle(m_callButton);
    refreshWidgetStyle(m_hangupButton);
}

void CallPage::updateUrlText()
{
    const QString url = callUrlText();
    const int margin = isCompact() ? 24 : 80;
    const int available = qMax(120, width() - margin);
    const QFontMetrics fm(m_urlText->font());
    m_urlText->setText(fm.elidedText(url, Qt::ElideMiddle, available));
    m_urlText->setToolTip(url);
}

void CallPage::refreshWidgetStyle(QWidget *widget)
{
    if (!widget) {
        return;
    }
    widget->style()->unpolish(widget);
    widget->style()->polish(widget);
    widget->update();
}

QString CallPage::callUrlText() const
{
    return CallClient::configuredUrl().toString();
}

QString CallPage::elapsedText() const
{
    if (!m_elapsed.isValid()) {
        return QStringLiteral("00:00");
    }
    return mmss(m_elapsed.elapsed() / 1000);
}

bool CallPage::isCompact() const
{
    return width() < 900 || height() < 520;
}
