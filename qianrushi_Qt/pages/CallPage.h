#pragma once

#include <QElapsedTimer>
#include <QWidget>

#include "core/CallClient.h"

class QLabel;
class QPushButton;
class QTimer;
class QFrame;
class QResizeEvent;

class CallPage : public QWidget
{
    Q_OBJECT

public:
    explicit CallPage(CallClient *callClient, QWidget *parent = nullptr);

private slots:
    void updateCallState(CallClient::State state);
    void updateStatusText(const QString &text);
    void updateMicMuted(bool muted);
    void updateSpeakerMuted(bool muted);
    void showCallError(const QString &message);
    void updateElapsedTime();
    void updateWaveform();

protected:
    void resizeEvent(QResizeEvent *event) override;

private:
    void buildUi();
    void applyState();
    void updateButtonTexts();
    void updateUrlText();
    void refreshWidgetStyle(QWidget *widget);
    QString callUrlText() const;
    QString elapsedText() const;
    bool isCompact() const;

    CallClient *m_callClient = nullptr;

    QLabel *m_title = nullptr;
    QLabel *m_subtitle = nullptr;
    QLabel *m_statusBadge = nullptr;
    QLabel *m_statusIcon = nullptr;
    QLabel *m_statusText = nullptr;
    QLabel *m_urlText = nullptr;
    QLabel *m_timerText = nullptr;
    QLabel *m_waveformLabel = nullptr;
    QLabel *m_hintText = nullptr;
    QLabel *m_lastEventText = nullptr;

    QPushButton *m_callButton = nullptr;
    QPushButton *m_micButton = nullptr;
    QPushButton *m_speakerButton = nullptr;
    QPushButton *m_hangupButton = nullptr;

    QFrame *m_panel = nullptr;
    QTimer *m_callTimer = nullptr;
    QTimer *m_waveformTimer = nullptr;

    CallClient::State m_state = CallClient::State::Disconnected;
    bool m_micMuted = false;
    bool m_speakerMuted = true;
    bool m_errorActive = false;
    QString m_lastError;
    QElapsedTimer m_elapsed;
    int m_waveformFrame = 0;
};
