#pragma once

#include <QByteArray>
#include <QObject>
#include <QUrl>
#include <QVariantMap>
#include <QtGlobal>

#if QT_VERSION >= QT_VERSION_CHECK(6, 0, 0)
class QAudioSink;
class QAudioSource;
#else
class QAudioInput;
class QAudioOutput;
#endif
class QIODevice;
class QTimer;
class QTcpSocket;

class VoiceClient : public QObject
{
    Q_OBJECT

public:
    explicit VoiceClient(QObject *parent = nullptr);
    ~VoiceClient() override;

    bool isRecording() const;

public slots:
    void connectToServer();
    void connectToServer(const QUrl &url);
    void startRecording();
    void stopRecording();
    void toggleRecording();
    void reset();
    void closeConnection();
    void stopPlayback();
    void speakToolResult(const QString &text);
    void sendEnvSnapshot(const QVariantMap &data);
    void suspendAudioForCall();
    void resumeAudioAfterCall();

signals:
    void connectedChanged(bool connected);
    void recordingChanged(bool recording);
    void stateChanged(const QString &state);
    void asrPartial(const QString &text);
    void asrFinal(const QString &text);
    void replyPartial(const QString &text);
    void replyText(const QString &text);
    void mapToolCall(const QString &query, const QString &command);
    void streamingStarted();
    void streamingFinished(const QString &fullText);
    void ttsError(const QString &message);
    void audioPlayingChanged(bool playing);
    void errorText(const QString &message);
    void audioInputStats(int levelPercent, qint64 bytesSent);
    void recordingSaved(const QString &path);
    void stm32CommandReceived(const QByteArray &payload);

private slots:
    void onConnected();
    void onReadyRead();
    void onDisconnected();

private:
    void sendText(const QByteArray &payload);
    void sendBinary(const QByteArray &payload);
    void sendFrame(quint8 opcode, const QByteArray &payload);
    void processHandshake();
    void processFrames();
    void handleTextMessage(const QByteArray &payload);
    void handleBinaryMessage(const QByteArray &payload);
    void beginAudioInput();
    void endAudioInput();
    void ensureAudioOutput(int sampleRate);
    void flushAudioOutput();
    void clearAudioOutput();
    void setAudioPlaying(bool playing);
    QByteArray buildPcmFrame(const QByteArray &pcm) const;
    void saveRecording();
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
    QTimer *m_playbackIdleTimer = nullptr;
    QTimer *m_playbackFlushTimer = nullptr;
    QUrl m_url;
    QByteArray m_buffer;
    QByteArray m_recordingPcm;
    QByteArray m_playbackBuffer;
    QString m_streamingText;
    qint64 m_sentBytes = 0;
    bool m_handshakeDone = false;
    bool m_recording = false;
    bool m_pendingStart = false;
    bool m_waitingForReply = false;
    bool m_closingAfterComplete = false;
    bool m_audioPlaying = false;
    int m_pendingPlaybackMs = 0;
    int m_captureSampleRate = 16000;
    int m_captureChannels = 1;
    int m_inputSampleRate = 16000;
    int m_ttsInputSampleRate = 0;
    int m_outputSampleRate = 0;
    int m_outputChannels = 1;
};
