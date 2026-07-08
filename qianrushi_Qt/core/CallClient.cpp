#include "CallClient.h"
#include "BackendConfig.h"

#include <QAudioFormat>
#if QT_VERSION >= QT_VERSION_CHECK(6, 0, 0)
#include <QAudioDevice>
#include <QAudioSink>
#include <QAudioSource>
#include <QMediaDevices>
#else
#include <QAudioDeviceInfo>
#include <QAudioInput>
#include <QAudioOutput>
#endif
#include <QDebug>
#include <QJsonDocument>
#include <QJsonObject>
#include <QRandomGenerator>
#include <QStringList>
#include <QTcpSocket>
#include <QTimer>
#include <QVector>

#include <cmath>

namespace {
constexpr int kCallSampleRate = 16000;
constexpr int kCallChannels = 1;
constexpr int kCallFrameMs = 20;
constexpr int kCallFrameBytes = kCallSampleRate * kCallChannels * 2 * kCallFrameMs / 1000;
constexpr int kMaxPlaybackFrames = 25;
constexpr int kMaxFramesPerProcess = 32;
constexpr int kPlaybackLogIntervalMs = 1000;

QAudioFormat pcm16Format(int sampleRate)
{
    QAudioFormat format;
    format.setSampleRate(sampleRate);
    format.setChannelCount(1);
#if QT_VERSION >= QT_VERSION_CHECK(6, 0, 0)
    format.setSampleFormat(QAudioFormat::Int16);
#else
    format.setSampleSize(16);
    format.setSampleType(QAudioFormat::SignedInt);
    format.setByteOrder(QAudioFormat::LittleEndian);
    format.setCodec(QStringLiteral("audio/pcm"));
#endif
    return format;
}

bool isPcm16Format(const QAudioFormat &format)
{
#if QT_VERSION >= QT_VERSION_CHECK(6, 0, 0)
    return format.sampleFormat() == QAudioFormat::Int16;
#else
    return format.sampleSize() == 16
        && format.sampleType() == QAudioFormat::SignedInt
        && format.byteOrder() == QAudioFormat::LittleEndian
        && (format.codec().isEmpty() || format.codec() == QStringLiteral("audio/pcm"));
#endif
}

QByteArray randomBytes(int size)
{
    QByteArray bytes;
    bytes.resize(size);
    for (int i = 0; i < size; ++i) {
        bytes[i] = static_cast<char>(QRandomGenerator::global()->bounded(256));
    }
    return bytes;
}

qint16 readLE16(const char *data)
{
    const auto *raw = reinterpret_cast<const uchar *>(data);
    return qint16(raw[0] | (raw[1] << 8));
}

void appendLE16(QByteArray &out, quint16 value)
{
    out.append(char(value & 0xFF));
    out.append(char((value >> 8) & 0xFF));
}

void appendSample(QByteArray &out, double value)
{
    const int rounded = int(std::lround(value));
    appendLE16(out, quint16(qBound(-32768, rounded, 32767)));
}

QByteArray normalizePcm16Mono16k(const QByteArray &pcm, int sampleRate, int channels)
{
    if (pcm.isEmpty()) {
        return {};
    }
    if (sampleRate <= 0) {
        sampleRate = kCallSampleRate;
    }
    if (channels <= 0) {
        channels = 1;
    }

    const int frameBytes = channels * 2;
    const int frameCount = pcm.size() / frameBytes;
    if (frameCount <= 0) {
        return {};
    }

    QVector<double> mono;
    mono.reserve(frameCount);
    const char *data = pcm.constData();
    for (int frame = 0; frame < frameCount; ++frame) {
        int sum = 0;
        for (int ch = 0; ch < channels; ++ch) {
            sum += int(readLE16(data + frame * frameBytes + ch * 2));
        }
        mono.append(double(sum) / double(channels));
    }

    if (sampleRate == kCallSampleRate) {
        QByteArray out;
        out.reserve(mono.size() * 2);
        for (double sample : mono) {
            appendSample(out, sample);
        }
        return out;
    }

    const int outCount = int(std::floor(double(mono.size()) * double(kCallSampleRate) / double(sampleRate)));
    if (outCount <= 0) {
        return {};
    }

    QByteArray out;
    out.reserve(outCount * 2);
    const double scale = double(sampleRate) / double(kCallSampleRate);
    for (int i = 0; i < outCount; ++i) {
        const double pos = double(i) * scale;
        const int left = int(pos);
        if (left >= mono.size() - 1) {
            appendSample(out, mono.last());
            continue;
        }
        const double frac = pos - double(left);
        appendSample(out, mono[left] * (1.0 - frac) + mono[left + 1] * frac);
    }
    return out;
}

QByteArray convertPcm16MonoForOutput(const QByteArray &pcm, int inputRate, int outputRate, int outputChannels)
{
    if (pcm.isEmpty()) {
        return {};
    }
    if (inputRate <= 0) {
        inputRate = kCallSampleRate;
    }
    if (outputRate <= 0) {
        outputRate = inputRate;
    }
    outputChannels = qMax(1, outputChannels);
    const int inputSamples = pcm.size() / 2;
    if (inputSamples <= 0) {
        return {};
    }
    if (inputRate == outputRate && outputChannels == 1) {
        return pcm.left(inputSamples * 2);
    }

    const char *data = pcm.constData();
    const int outputSamples = qMax(1, int(std::floor(double(inputSamples) * double(outputRate) / double(inputRate))));
    QByteArray out;
    out.reserve(outputSamples * outputChannels * 2);
    const double scale = double(inputRate) / double(outputRate);
    for (int i = 0; i < outputSamples; ++i) {
        const double pos = double(i) * scale;
        const int left = int(pos);
        double value = 0.0;
        if (left >= inputSamples - 1) {
            value = readLE16(data + (inputSamples - 1) * 2);
        } else {
            const double frac = pos - double(left);
            value = double(readLE16(data + left * 2)) * (1.0 - frac)
                + double(readLE16(data + (left + 1) * 2)) * frac;
        }
        for (int ch = 0; ch < outputChannels; ++ch) {
            appendSample(out, value);
        }
    }
    return out;
}

QString envString(const char *name)
{
    return QString::fromLocal8Bit(qgetenv(name)).trimmed();
}

quint16 envPort(const char *name, quint16 fallback)
{
    bool ok = false;
    const int value = qgetenv(name).trimmed().toInt(&ok);
    if (!ok || value <= 0 || value > 65535) {
        return fallback;
    }
    return quint16(value);
}

QString envPath(const char *name, const QString &fallback)
{
    QString path = envString(name);
    if (path.isEmpty()) {
        path = fallback;
    }
    if (!path.startsWith(QLatin1Char('/'))) {
        path.prepend(QLatin1Char('/'));
    }
    return path;
}

#if QT_VERSION < QT_VERSION_CHECK(6, 0, 0)
QStringList qt5AudioDeviceNames(const QList<QAudioDeviceInfo> &devices)
{
    QStringList names;
    for (const QAudioDeviceInfo &device : devices) {
        names.append(device.deviceName());
    }
    return names;
}

int qt5AudioDevicePriority(const QString &name)
{
    if (name.contains(QStringLiteral("plughw:CARD=Y1076,DEV=0"), Qt::CaseInsensitive)) return 0;
    if (name.contains(QStringLiteral("hw:CARD=Y1076,DEV=0"), Qt::CaseInsensitive)) return 1;
    if (name.contains(QStringLiteral("sysdefault:CARD=Y1076"), Qt::CaseInsensitive)) return 2;
    if (name.contains(QStringLiteral("Yundea"), Qt::CaseInsensitive)) return 3;
    if (name.contains(QStringLiteral("usb"), Qt::CaseInsensitive)) return 4;
    if (name.contains(QStringLiteral("pulse"), Qt::CaseInsensitive)) return 90;
    return 100;
}

QAudioDeviceInfo chooseQt5AudioDevice(QAudio::Mode mode, const QAudioDeviceInfo &defaultDevice, const char *label)
{
    const QList<QAudioDeviceInfo> devices = QAudioDeviceInfo::availableDevices(mode);
    qDebug().noquote() << "CallClient Qt5" << label << "devices:" << qt5AudioDeviceNames(devices).join(QStringLiteral(", "));
    QAudioDeviceInfo selected;
    int selectedPriority = 1000;
    for (const QAudioDeviceInfo &device : devices) {
        const int priority = qt5AudioDevicePriority(device.deviceName());
        if (priority < selectedPriority) {
            selected = device;
            selectedPriority = priority;
        }
    }
    if (selected.isNull() && !defaultDevice.isNull()) {
        selected = defaultDevice;
    }
    qDebug().noquote() << "CallClient Qt5 selected" << label << "device:"
                       << (selected.isNull() ? QStringLiteral("<none>") : selected.deviceName());
    return selected;
}
#endif
}

CallClient::CallClient(QObject *parent)
    : QObject(parent)
    , m_socket(new QTcpSocket(this))
    , m_playbackTimer(new QTimer(this))
    , m_url(configuredUrl())
{
    m_playbackTimer->setInterval(kCallFrameMs);
    m_playbackTimer->setTimerType(Qt::PreciseTimer);
    connect(m_playbackTimer, &QTimer::timeout, this, &CallClient::onPlaybackTick);
    connect(m_socket, &QTcpSocket::connected, this, &CallClient::onConnected);
    connect(m_socket, &QTcpSocket::readyRead, this, &CallClient::onReadyRead);
    connect(m_socket, &QTcpSocket::disconnected, this, &CallClient::onDisconnected);
    connect(m_socket, &QTcpSocket::errorOccurred, this, [this](QAbstractSocket::SocketError) {
        emit errorText(m_socket->errorString());
        hangup();
    });
    m_playbackLogTimer.start();
}

CallClient::~CallClient()
{
    hangup();
}

QUrl CallClient::configuredUrl()
{
    QString host = envString("SMARTNAV_CALL_HOST");
    if (host.isEmpty()) {
        host = envString("SMARTNAV_MQTT_HOST");
    }
    if (host.isEmpty()) {
        host = envString("VOICE_MQTT_BROKER");
    }
    if (host.isEmpty()) {
        host = BackendConfig::host();
    }

    const quint16 port = envPort("SMARTNAV_CALL_PORT", 8090);
    QUrl url;
    url.setScheme(QStringLiteral("ws"));
    url.setHost(host);
    url.setPort(port);
    url.setPath(envPath("SMARTNAV_CALL_PATH", QStringLiteral("/call")));
    return url;
}

QString CallClient::stateText(State state)
{
    switch (state) {
    case State::Connecting:
        return QStringLiteral("连接中");
    case State::InCall:
        return QStringLiteral("通话中");
    case State::Disconnected:
    default:
        return QStringLiteral("已断开");
    }
}

CallClient::State CallClient::state() const { return m_state; }
bool CallClient::isMicMuted() const { return m_micMuted; }
bool CallClient::isSpeakerMuted() const { return m_speakerMuted; }

void CallClient::startCall()
{
    if (m_state != State::Disconnected) {
        return;
    }
    m_url = configuredUrl();
    m_handshakeDone = false;
    m_buffer.clear();
    m_captureBuffer.clear();
    clearPlaybackQueue();
    m_processFramesQueued = false;
    setState(State::Connecting);
    qDebug().noquote() << "CallClient connecting to" << m_url.toString();
    m_socket->connectToHost(m_url.host(), m_url.port(80));
}

void CallClient::hangup()
{
    endAudioInput();
    clearAudioOutput();
    m_handshakeDone = false;
    m_buffer.clear();
    m_captureBuffer.clear();
    m_processFramesQueued = false;
    if (m_socket->state() != QAbstractSocket::UnconnectedState) {
        m_socket->disconnectFromHost();
    }
    setState(State::Disconnected);
}

void CallClient::toggleCall()
{
    if (m_state == State::Disconnected) {
        startCall();
    } else {
        hangup();
    }
}

void CallClient::setMicMuted(bool muted)
{
    if (!muted && !m_speakerMuted) {
        m_speakerMuted = true;
        clearAudioOutput();
        emit speakerMutedChanged(m_speakerMuted);
    }
    if (m_micMuted == muted) {
        return;
    }
    m_micMuted = muted;
    emit micMutedChanged(m_micMuted);
}

void CallClient::toggleMicMuted()
{
    setMicMuted(!m_micMuted);
}

void CallClient::setSpeakerMuted(bool muted)
{
    if (!muted && !m_micMuted) {
        m_micMuted = true;
        emit micMutedChanged(m_micMuted);
    }
    if (m_speakerMuted == muted) {
        return;
    }
    m_speakerMuted = muted;
    if (m_speakerMuted) {
        clearAudioOutput();
        clearPlaybackQueue();
    }
    emit speakerMutedChanged(m_speakerMuted);
}

void CallClient::toggleSpeakerMuted()
{
    setSpeakerMuted(!m_speakerMuted);
}

void CallClient::onConnected()
{
    m_socket->write(buildHandshake());
}

void CallClient::onReadyRead()
{
    m_buffer.append(m_socket->readAll());
    if (!m_handshakeDone) {
        processHandshake();
    }
    if (m_handshakeDone) {
        processFrames();
    }
}

void CallClient::onDisconnected()
{
    endAudioInput();
    clearAudioOutput();
    m_handshakeDone = false;
    m_processFramesQueued = false;
    if (m_state != State::Disconnected) {
        setState(State::Disconnected);
    }
}

void CallClient::setState(State state)
{
    if (m_state == state) {
        emit statusTextChanged(stateText(state));
        return;
    }
    const State previous = m_state;
    m_state = state;
    emit stateChanged(m_state);
    emit statusTextChanged(stateText(m_state));
    if (m_state == State::InCall) {
        emit callStarted();
    } else if (previous == State::InCall) {
        emit callEnded();
    }
}

void CallClient::beginAudioInput()
{
    if (m_audioSource) {
        return;
    }
#if QT_VERSION >= QT_VERSION_CHECK(6, 0, 0)
    const QAudioDevice inputDevice = QMediaDevices::defaultAudioInput();
#else
    const QAudioDeviceInfo inputDevice = chooseQt5AudioDevice(QAudio::AudioInput, QAudioDeviceInfo::defaultInputDevice(), "input");
#endif
    if (inputDevice.isNull()) {
        emit errorText(QStringLiteral("没有可用麦克风设备"));
        return;
    }

#if QT_VERSION >= QT_VERSION_CHECK(6, 0, 0)
    QAudioFormat format = pcm16Format(kCallSampleRate);
    if (!inputDevice.isFormatSupported(format)) {
        format = inputDevice.preferredFormat();
        if (!isPcm16Format(format)) {
            emit errorText(QStringLiteral("通话麦克风不支持 PCM16"));
            return;
        }
    }
#else
    QAudioFormat format = inputDevice.preferredFormat();
    if (!isPcm16Format(format) || format.sampleRate() <= 0 || format.channelCount() <= 0) {
        format = inputDevice.nearestFormat(pcm16Format(kCallSampleRate));
    }
    if (!isPcm16Format(format) || format.sampleRate() <= 0 || format.channelCount() <= 0) {
        emit errorText(QStringLiteral("通话麦克风不支持 PCM16"));
        return;
    }
#endif

    m_captureSampleRate = format.sampleRate();
    m_captureChannels = qMax(1, format.channelCount());
#if QT_VERSION >= QT_VERSION_CHECK(6, 0, 0)
    m_audioSource = new QAudioSource(inputDevice, format, this);
#else
    m_audioSource = new QAudioInput(inputDevice, format, this);
#endif
    m_audioSource->setBufferSize(kCallFrameBytes * 6);
    m_audioInput = m_audioSource->start();
    if (!m_audioInput) {
        emit errorText(QStringLiteral("无法打开通话麦克风"));
        delete m_audioSource;
        m_audioSource = nullptr;
        return;
    }

    connect(m_audioInput, &QIODevice::readyRead, this, [this]() {
        const QByteArray captured = m_audioInput->readAll();
        const QByteArray pcm = normalizePcm16Mono16k(captured, m_captureSampleRate, m_captureChannels);
        if (pcm.isEmpty()) {
            return;
        }
        m_captureBuffer.append(pcm);
        while (m_captureBuffer.size() >= kCallFrameBytes) {
            const QByteArray frame = m_captureBuffer.left(kCallFrameBytes);
            m_captureBuffer.remove(0, kCallFrameBytes);
            if (!m_micMuted) {
                sendBinary(frame);
            }
        }
    });
}

void CallClient::endAudioInput()
{
    if (m_audioSource) {
        if (m_audioInput) {
            disconnect(m_audioInput, nullptr, this, nullptr);
        }
        m_audioSource->stop();
        delete m_audioSource;
        m_audioSource = nullptr;
        m_audioInput = nullptr;
    }
}

void CallClient::ensureAudioOutput(int sampleRate)
{
    if (m_speakerMuted || sampleRate <= 0) {
        return;
    }
    if (m_audioSink && m_outputSampleRate == sampleRate) {
        return;
    }
    if (m_audioSink) {
        disconnect(m_audioSink, nullptr, this, nullptr);
        m_audioSink->stop();
        delete m_audioSink;
        m_audioSink = nullptr;
        m_audioOutput = nullptr;
        m_outputSampleRate = 0;
        m_outputChannels = 1;
    }
#if QT_VERSION >= QT_VERSION_CHECK(6, 0, 0)
    const QAudioDevice outputDevice = QMediaDevices::defaultAudioOutput();
#else
    const QAudioDeviceInfo outputDevice = chooseQt5AudioDevice(QAudio::AudioOutput, QAudioDeviceInfo::defaultOutputDevice(), "output");
#endif
    if (outputDevice.isNull()) {
        emit errorText(QStringLiteral("没有可用音频输出设备"));
        return;
    }
    QAudioFormat format = outputDevice.preferredFormat();
    if (!isPcm16Format(format) || format.sampleRate() <= 0 || format.channelCount() <= 0) {
        format = pcm16Format(sampleRate);
    }
    if (!outputDevice.isFormatSupported(format)) {
        format = outputDevice.nearestFormat(pcm16Format(sampleRate));
    }
    if (!isPcm16Format(format) || format.sampleRate() <= 0 || format.channelCount() <= 0) {
        emit errorText(QStringLiteral("通话音频输出不支持 PCM16"));
        return;
    }
    m_outputSampleRate = format.sampleRate();
    m_outputChannels = qMax(1, format.channelCount());
#if QT_VERSION >= QT_VERSION_CHECK(6, 0, 0)
    m_audioSink = new QAudioSink(outputDevice, format, this);
#else
    m_audioSink = new QAudioOutput(outputDevice, format, this);
#endif
    m_audioSink->setBufferSize(m_outputSampleRate * m_outputChannels * 2 / 2);
    m_audioOutput = m_audioSink->start();
    if (!m_audioOutput) {
        emit errorText(QStringLiteral("无法打开通话音频输出"));
        clearAudioOutput();
    }
}

void CallClient::clearAudioOutput()
{
    clearPlaybackQueue();
    if (m_playbackTimer) {
        m_playbackTimer->stop();
    }
    if (m_audioSink) {
        disconnect(m_audioSink, nullptr, this, nullptr);
        m_audioSink->stop();
        delete m_audioSink;
        m_audioSink = nullptr;
        m_audioOutput = nullptr;
        m_outputSampleRate = 0;
        m_outputChannels = 1;
    }
}

void CallClient::clearPlaybackQueue()
{
    m_playbackQueue.clear();
}

void CallClient::processHandshake()
{
    const int end = m_buffer.indexOf("\r\n\r\n");
    if (end < 0) {
        return;
    }
    const QByteArray header = m_buffer.left(end);
    m_buffer.remove(0, end + 4);
    if (!header.startsWith("HTTP/1.1 101") && !header.startsWith("HTTP/1.0 101")) {
        emit errorText(QString::fromUtf8(header.left(160)));
        hangup();
        return;
    }
    m_handshakeDone = true;
    sendText(QByteArrayLiteral("{\"type\":\"hello\",\"role\":\"device\",\"deviceId\":\"lubancat\"}"));
    setState(State::Connecting);
    emit statusTextChanged(QStringLiteral("等待工作人员接入"));
}

void CallClient::processFrames()
{
    m_processFramesQueued = false;
    int processed = 0;
    while (m_buffer.size() >= 2 && processed < kMaxFramesPerProcess) {
        const quint8 b0 = quint8(m_buffer[0]);
        const quint8 b1 = quint8(m_buffer[1]);
        quint64 len = b1 & 0x7F;
        int offset = 2;
        if (len == 126) {
            if (m_buffer.size() < offset + 2) return;
            len = (quint8(m_buffer[offset]) << 8) | quint8(m_buffer[offset + 1]);
            offset += 2;
        } else if (len == 127) {
            if (m_buffer.size() < offset + 8) return;
            len = 0;
            for (int i = 0; i < 8; ++i) {
                len = (len << 8) | quint8(m_buffer[offset + i]);
            }
            offset += 8;
        }
        const bool masked = (b1 & 0x80) != 0;
        QByteArray mask;
        if (masked) {
            if (m_buffer.size() < offset + 4) return;
            mask = m_buffer.mid(offset, 4);
            offset += 4;
        }
        if (len > quint64(m_buffer.size() - offset)) {
            return;
        }
        QByteArray payload = m_buffer.mid(offset, int(len));
        m_buffer.remove(0, offset + int(len));
        if (masked) {
            for (int i = 0; i < payload.size(); ++i) {
                payload[i] = char(payload[i] ^ mask.at(i % 4));
            }
        }
        const quint8 opcode = b0 & 0x0F;
        if (opcode == 0x1) {
            handleTextMessage(payload);
        } else if (opcode == 0x2) {
            handleBinaryMessage(payload);
        } else if (opcode == 0x8) {
            hangup();
            return;
        } else if (opcode == 0x9) {
            sendFrame(0xA, payload);
        } else if (opcode == 0xA) {
            // pong received; no state change needed
        }
        ++processed;
    }
    if (processed >= kMaxFramesPerProcess && !m_buffer.isEmpty() && !m_processFramesQueued) {
        m_processFramesQueued = true;
        QTimer::singleShot(0, this, [this]() {
            if (m_handshakeDone) {
                processFrames();
            } else {
                m_processFramesQueued = false;
            }
        });
    }
}

void CallClient::handleBinaryMessage(const QByteArray &payload)
{
    m_logInboundBinaryBytes += payload.size();
    ++m_logInboundBinaryMessages;

    if (m_speakerMuted) {
        clearPlaybackQueue();
        logPlaybackStats(false);
        return;
    }

    if (payload.size() == kCallFrameBytes) {
        enqueuePlaybackFrame(payload);
    } else if (payload.size() > 0 && payload.size() % kCallFrameBytes == 0) {
        qWarning().noquote() << "CallClient inbound binary contains multiple PCM frames, bytes="
                             << payload.size() << "frameBytes=" << kCallFrameBytes
                             << "frames=" << (payload.size() / kCallFrameBytes)
                             << "server should send one 20ms frame per WebSocket message";
        for (int offset = 0; offset < payload.size(); offset += kCallFrameBytes) {
            enqueuePlaybackFrame(payload.mid(offset, kCallFrameBytes));
        }
    } else {
        ++m_logInvalidBinaryMessages;
        qWarning().noquote() << "CallClient dropping invalid inbound binary audio, bytes="
                             << payload.size() << "expectedFrameBytes=" << kCallFrameBytes
                             << "format=PCM16LE/16000Hz/mono/20ms";
    }
    logPlaybackStats(false);
}

void CallClient::enqueuePlaybackFrame(const QByteArray &frame)
{
    if (frame.size() != kCallFrameBytes) {
        ++m_logInvalidBinaryMessages;
        qWarning().noquote() << "CallClient refusing non-640-byte playback frame, bytes="
                             << frame.size() << "expectedFrameBytes=" << kCallFrameBytes;
        return;
    }
    while (m_playbackQueue.size() >= kMaxPlaybackFrames) {
        m_playbackQueue.dequeue();
        ++m_logDroppedFrames;
    }
    m_playbackQueue.enqueue(frame);
    ++m_logValidFrames;
    if (!m_playbackTimer->isActive()) {
        m_playbackTimer->start();
    }
}

void CallClient::onPlaybackTick()
{
    if (m_speakerMuted) {
        clearPlaybackQueue();
        if (m_playbackTimer->isActive()) {
            m_playbackTimer->stop();
        }
        logPlaybackStats(false);
        return;
    }
    if (m_playbackQueue.isEmpty()) {
        if (m_playbackTimer->isActive()) {
            m_playbackTimer->stop();
        }
        logPlaybackStats(false);
        return;
    }

    ensureAudioOutput(kCallSampleRate);
    if (!m_audioSink || !m_audioOutput) {
        logPlaybackStats(false);
        return;
    }

    const QByteArray &frame = m_playbackQueue.head();
    const QByteArray out = convertPcm16MonoForOutput(frame, kCallSampleRate, m_outputSampleRate, m_outputChannels);
    if (out.isEmpty()) {
        m_playbackQueue.dequeue();
        logPlaybackStats(false);
        return;
    }

    const int freeBytes = m_audioSink->bytesFree();
    if (freeBytes < out.size()) {
        logPlaybackStats(false);
        return;
    }

    const qint64 written = m_audioOutput->write(out);
    if (written > 0) {
        m_playbackQueue.dequeue();
    }
    logPlaybackStats(false);
}

void CallClient::logPlaybackStats(bool force)
{
    if (!force && m_playbackLogTimer.isValid() && m_playbackLogTimer.elapsed() < kPlaybackLogIntervalMs) {
        return;
    }
    const int queueFrames = m_playbackQueue.size();
    const int queueMs = queueFrames * kCallFrameMs;
    const int bytesFree = m_audioSink ? m_audioSink->bytesFree() : -1;
    qDebug().noquote() << "CallClient audio rx stats:"
                       << "inboundMessages=" << m_logInboundBinaryMessages
                       << "inboundBytes=" << m_logInboundBinaryBytes
                       << "valid640Frames=" << m_logValidFrames
                       << "invalidBinaryMessages=" << m_logInvalidBinaryMessages
                       << "queueFrames=" << queueFrames
                       << "queueMs=" << queueMs
                       << "droppedFrames=" << m_logDroppedFrames
                       << "audioOutputBytesFree=" << bytesFree;
    m_logInboundBinaryBytes = 0;
    m_logInboundBinaryMessages = 0;
    m_logValidFrames = 0;
    m_logInvalidBinaryMessages = 0;
    m_logDroppedFrames = 0;
    m_playbackLogTimer.restart();
}

void CallClient::handleTextMessage(const QByteArray &payload)
{
    const QJsonDocument doc = QJsonDocument::fromJson(payload);
    if (!doc.isObject()) {
        return;
    }
    const QJsonObject obj = doc.object();
    const QString type = obj.value(QStringLiteral("type")).toString();
    if (type == QStringLiteral("call_end")) {
        hangup();
    } else if (type == QStringLiteral("operator_joined")
               || type == QStringLiteral("paired")
               || type == QStringLiteral("call_ready")) {
        if (m_state != State::InCall) {
            setState(State::InCall);
            beginAudioInput();
        }
    } else if (type == QStringLiteral("waiting") || type == QStringLiteral("operator_waiting")) {
        setState(State::Connecting);
        emit statusTextChanged(QStringLiteral("等待工作人员接入"));
    } else if (type == QStringLiteral("error")) {
        emit errorText(obj.value(QStringLiteral("message")).toString(QStringLiteral("通话服务错误")));
        hangup();
    } else if (type == QStringLiteral("ping")) {
        sendText(QByteArrayLiteral("{\"type\":\"pong\"}"));
    } else if (type == QStringLiteral("pong")) {
        // application-level pong; no state change needed
    }
}

void CallClient::sendFrame(quint8 opcode, const QByteArray &payload)
{
    if (!m_handshakeDone || m_socket->state() != QAbstractSocket::ConnectedState) {
        return;
    }
    QByteArray frame;
    frame.append(char(0x80 | opcode));
    const int size = payload.size();
    if (size < 126) {
        frame.append(char(0x80 | size));
    } else if (size <= 0xFFFF) {
        frame.append(char(0x80 | 126));
        frame.append(char((size >> 8) & 0xFF));
        frame.append(char(size & 0xFF));
    } else {
        frame.append(char(0x80 | 127));
        for (int i = 7; i >= 0; --i) {
            frame.append(char((quint64(size) >> (8 * i)) & 0xFF));
        }
    }
    const QByteArray mask = randomBytes(4);
    frame.append(mask);
    QByteArray masked = payload;
    for (int i = 0; i < masked.size(); ++i) {
        masked[i] = char(masked[i] ^ mask.at(i % 4));
    }
    frame.append(masked);
    m_socket->write(frame);
}

void CallClient::sendText(const QByteArray &payload)
{
    sendFrame(0x1, payload);
}

void CallClient::sendBinary(const QByteArray &payload)
{
    sendFrame(0x2, payload);
}

QByteArray CallClient::buildHandshake() const
{
    const QByteArray key = randomBytes(16).toBase64();
    const QString path = m_url.path().isEmpty() ? QStringLiteral("/") : m_url.path();
    QByteArray req;
    req += "GET " + path.toUtf8() + " HTTP/1.1\r\n";
    req += "Host: " + m_url.host().toUtf8() + ":" + QByteArray::number(m_url.port(80)) + "\r\n";
    req += "Upgrade: websocket\r\n";
    req += "Connection: Upgrade\r\n";
    req += "Sec-WebSocket-Key: " + key + "\r\n";
    req += "Sec-WebSocket-Version: 13\r\n";
    req += "\r\n";
    return req;
}
