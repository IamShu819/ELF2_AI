#pragma once

#include <QByteArray>
#include <QElapsedTimer>
#include <QQueue>
#include <QObject>
#include <QUrl>
#include <QtGlobal>

#if QT_VERSION >= QT_VERSION_CHECK(6, 0, 0)
class QAudioSink;
class QAudioSource;
#else
class QAudioInput;
class QAudioOutput;
#endif
class QIODevice;
class QTcpSocket;
class QTimer;

class CallClient : public QObject
{
    Q_OBJECT

public:
    enum class State {
        Disconnected,
        Connecting,
        InCall
    };
    Q_ENUM(State)

    explicit CallClient(QObject *parent = nullptr);
    ~CallClient() override;

    static QUrl configuredUrl();
    static QString stateText(State state);

    State state() const;
    bool isMicMuted() const;
    bool isSpeakerMuted() const;

public slots:
    void startCall();
    void hangup();
    void toggleCall();
    void setMicMuted(bool muted);
    void toggleMicMuted();
    void setSpeakerMuted(bool muted);
    void toggleSpeakerMuted();

signals:
    void stateChanged(CallClient::State state);
    void statusTextChanged(const QString &text);
    void micMutedChanged(bool muted);
    void speakerMutedChanged(bool muted);
    void errorText(const QString &message);
    void callStarted();
    void callEnded();

private slots:
    void onConnected();
    void onReadyRead();
    void onDisconnected();

private:
    void setState(State state);
    void beginAudioInput();
    void endAudioInput();
    void ensureAudioOutput(int sampleRate);
    void clearAudioOutput();
    void clearPlaybackQueue();
    void processHandshake();
    void processFrames();
    void handleTextMessage(const QByteArray &payload);
    void handleBinaryMessage(const QByteArray &payload);
    void enqueuePlaybackFrame(const QByteArray &frame);
    void onPlaybackTick();
    void logPlaybackStats(bool force = false);
    void sendFrame(quint8 opcode, const QByteArray &payload);
    void sendText(const QByteArray &payload);
    void sendBinary(const QByteArray &payload);
    QByteArray buildHandshake() const;

    QTcpSocket *m_socket = nullptr;
#if QT_VERSION >= QT_VERSION_CHECK(6, 0, 0)
    QAudioSource *m_audioSource = nullptr;
    QAudioSink *m_audioSink = nullptr;
#else
    QAudioInput *m_audioSource = nullptr;
    QAudioOutput *m_audioSink = nullptr;
#endif
    QIODevice *m_audioInput = nullptr;
    QIODevice *m_audioOutput = nullptr;
    QUrl m_url;
    QByteArray m_buffer;
    QByteArray m_captureBuffer;
    QQueue<QByteArray> m_playbackQueue;
    QTimer *m_playbackTimer = nullptr;
    State m_state = State::Disconnected;
    bool m_handshakeDone = false;
    bool m_micMuted = false;
    bool m_speakerMuted = true;
    bool m_processFramesQueued = false;
    int m_captureSampleRate = 16000;
    int m_captureChannels = 1;
    int m_outputSampleRate = 0;
    int m_outputChannels = 1;
    QElapsedTimer m_playbackLogTimer;
    qint64 m_logInboundBinaryBytes = 0;
    int m_logInboundBinaryMessages = 0;
    int m_logValidFrames = 0;
    int m_logInvalidBinaryMessages = 0;
    int m_logDroppedFrames = 0;
};
