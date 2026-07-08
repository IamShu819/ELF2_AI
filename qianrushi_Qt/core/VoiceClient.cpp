#include "VoiceClient.h"
#include "BackendConfig.h"

#include <QAudioFormat>
#if QT_VERSION >= QT_VERSION_CHECK(6, 0, 0)
#include <QAudioDevice>
#include <QAudioSink>
#include <QAudioSource>
#else
#include <QAudioDeviceInfo>
#include <QAudioInput>
#include <QAudioOutput>
#endif
#include <QDateTime>
#include <QDebug>
#include <QDir>
#include <QFile>
#include <QJsonDocument>
#include <QJsonObject>
#if QT_VERSION >= QT_VERSION_CHECK(6, 0, 0)
#include <QMediaDevices>
#endif
#include <QRandomGenerator>
#include <QStandardPaths>
#include <QStringList>
#include <QTcpSocket>
#include <QTimer>
#include <QVector>

#include <cmath>

namespace {
constexpr int kInputSampleRate = 16000;

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


#if QT_VERSION < QT_VERSION_CHECK(6, 0, 0)
bool isPulseQt5AudioDeviceName(const QString &name)
{
    return name.contains(QStringLiteral("pulse"), Qt::CaseInsensitive);
}

int qt5AudioDevicePriority(const QString &name)
{
    if (name.contains(QStringLiteral("plughw:CARD=Y1076,DEV=0"), Qt::CaseInsensitive)) {
        return 0;
    }
    if (name.contains(QStringLiteral("hw:CARD=Y1076,DEV=0"), Qt::CaseInsensitive)) {
        return 1;
    }
    if (name.contains(QStringLiteral("sysdefault:CARD=Y1076"), Qt::CaseInsensitive)) {
        return 2;
    }
    if (name.contains(QStringLiteral("Yundea"), Qt::CaseInsensitive)) {
        return 3;
    }
    if (name.contains(QStringLiteral("usb"), Qt::CaseInsensitive)) {
        return 4;
    }
    return 100;
}

QStringList qt5AudioDeviceNames(const QList<QAudioDeviceInfo> &devices)
{
    QStringList names;
    for (const QAudioDeviceInfo &device : devices) {
        names.append(device.deviceName());
    }
    return names;
}

QAudioDeviceInfo chooseQt5AudioDevice(QAudio::Mode mode, const QAudioDeviceInfo &defaultDevice, const char *label)
{
    const QList<QAudioDeviceInfo> devices = QAudioDeviceInfo::availableDevices(mode);
    qDebug().noquote() << "VoiceClient Qt5" << label << "devices:" << qt5AudioDeviceNames(devices).join(QStringLiteral(", "));

    QAudioDeviceInfo selected;
    int selectedPriority = 1000;
    for (const QAudioDeviceInfo &device : devices) {
        if (isPulseQt5AudioDeviceName(device.deviceName())) {
            continue;
        }
        const int priority = qt5AudioDevicePriority(device.deviceName());
        if (priority < selectedPriority) {
            selected = device;
            selectedPriority = priority;
        }
    }

    if (!selected.isNull()) {
        qDebug().noquote() << "VoiceClient Qt5 selected" << label << "device:" << selected.deviceName();
        return selected;
    }

    if (!defaultDevice.isNull() && !isPulseQt5AudioDeviceName(defaultDevice.deviceName())) {
        qDebug().noquote() << "VoiceClient Qt5 selected" << label << "device:" << defaultDevice.deviceName();
        return defaultDevice;
    }

    qDebug().noquote() << "VoiceClient Qt5 selected" << label << "device: <none>";
    return QAudioDeviceInfo();
}
#endif

QByteArray randomBytes(int size)
{
    QByteArray bytes;
    bytes.resize(size);
    for (int i = 0; i < size; ++i) {
        bytes[i] = static_cast<char>(QRandomGenerator::global()->bounded(256));
    }
    return bytes;
}

int peakPercent(const QByteArray &pcm)
{
    int peak = 0;
    const auto *data = reinterpret_cast<const uchar *>(pcm.constData());
    for (qsizetype i = 0; i + 1 < pcm.size(); i += 2) {
        const qint16 sample = qint16(data[i] | (data[i + 1] << 8));
        peak = qMax(peak, qAbs(int(sample)));
    }
    return qMin(100, int(double(peak) * 100.0 / 32768.0));
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
        sampleRate = kInputSampleRate;
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

    if (sampleRate == kInputSampleRate) {
        QByteArray out;
        out.reserve(mono.size() * 2);
        for (double sample : mono) {
            appendSample(out, sample);
        }
        return out;
    }

    const int outCount = int(std::floor(double(mono.size()) * double(kInputSampleRate) / double(sampleRate)));
    if (outCount <= 0) {
        return {};
    }

    QByteArray out;
    out.reserve(outCount * 2);
    const double scale = double(sampleRate) / double(kInputSampleRate);
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
        inputRate = outputRate > 0 ? outputRate : kInputSampleRate;
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
            const double a = readLE16(data + left * 2);
            const double b = readLE16(data + (left + 1) * 2);
            value = a * (1.0 - frac) + b * frac;
        }
        for (int ch = 0; ch < outputChannels; ++ch) {
            appendSample(out, value);
        }
    }
    return out;
}

void appendLE32(QByteArray &out, quint32 value)
{
    out.append(char(value & 0xFF));
    out.append(char((value >> 8) & 0xFF));
    out.append(char((value >> 16) & 0xFF));
    out.append(char((value >> 24) & 0xFF));
}
}

VoiceClient::VoiceClient(QObject *parent)
    : QObject(parent)
    , m_socket(new QTcpSocket(this))
    , m_playbackIdleTimer(new QTimer(this))
    , m_playbackFlushTimer(new QTimer(this))
{
    m_url = BackendConfig::voiceUrl();
    m_playbackIdleTimer->setSingleShot(true);
    m_playbackIdleTimer->setInterval(900);
    m_playbackFlushTimer->setInterval(20);
    connect(m_socket, &QTcpSocket::connected, this, &VoiceClient::onConnected);
    connect(m_socket, &QTcpSocket::readyRead, this, &VoiceClient::onReadyRead);
    connect(m_socket, &QTcpSocket::disconnected, this, &VoiceClient::onDisconnected);
    connect(m_socket, &QTcpSocket::errorOccurred, this, [this](QAbstractSocket::SocketError) {
        if (m_pendingStart || m_recording || m_waitingForReply) {
            emit errorText(m_socket->errorString());
        }
    });
    connect(m_playbackIdleTimer, &QTimer::timeout, this, [this]() {
        if (m_playbackBuffer.isEmpty()) {
            clearAudioOutput();
        }
    });
    connect(m_playbackFlushTimer, &QTimer::timeout, this, [this]() {
        flushAudioOutput();
    });
}

VoiceClient::~VoiceClient()
{
    endAudioInput();
    clearAudioOutput();
}

bool VoiceClient::isRecording() const
{
    return m_recording;
}

void VoiceClient::connectToServer(const QUrl &url)
{
    m_url = url;
    if (m_socket->state() == QAbstractSocket::ConnectedState || m_socket->state() == QAbstractSocket::ConnectingState) {
        return;
    }

    m_handshakeDone = false;
    m_buffer.clear();
    const int port = m_url.port(80);
    m_socket->connectToHost(m_url.host(), port);
}

void VoiceClient::connectToServer()
{
    connectToServer(m_url);
}

void VoiceClient::startRecording()
{
    if (m_recording) {
        return;
    }
    if (m_audioPlaying || m_waitingForReply) {
        clearAudioOutput();
        if (m_handshakeDone) {
            sendText(QByteArrayLiteral("{\"type\":\"stop\"}"));
        }
        m_waitingForReply = false;
    }
    m_pendingStart = true;
    m_closingAfterComplete = false;
    if (!m_handshakeDone) {
        connectToServer(m_url);
        return;
    }
    beginAudioInput();
}

void VoiceClient::stopRecording()
{
    if (!m_recording && !m_pendingStart) {
        return;
    }
    m_pendingStart = false;
    endAudioInput();
    if (m_handshakeDone) {
        m_waitingForReply = true;
        sendText(QByteArrayLiteral("{\"type\":\"finish\"}"));
    }
}

void VoiceClient::toggleRecording()
{
    if (m_recording) {
        stopRecording();
    } else {
        startRecording();
    }
}

void VoiceClient::reset()
{
    endAudioInput();
    clearAudioOutput();
    m_waitingForReply = false;
    m_streamingText.clear();
    if (m_handshakeDone) {
        sendText(QByteArrayLiteral("{\"type\":\"reset\"}"));
    }
}

void VoiceClient::closeConnection()
{
    endAudioInput();
    m_pendingStart = false;
    m_waitingForReply = false;
    m_handshakeDone = false;
    m_closingAfterComplete = true;
    m_streamingText.clear();
    m_buffer.clear();
    if (m_socket->state() != QAbstractSocket::UnconnectedState) {
        m_socket->disconnectFromHost();
    }
}

void VoiceClient::stopPlayback()
{
    clearAudioOutput();
}

void VoiceClient::speakToolResult(const QString &text)
{
    const QString trimmed = text.trimmed();
    if (trimmed.isEmpty() || !m_handshakeDone) {
        return;
    }

    QJsonObject obj;
    obj.insert(QStringLiteral("type"), QStringLiteral("tool_result"));
    obj.insert(QStringLiteral("text"), trimmed);
    sendText(QJsonDocument(obj).toJson(QJsonDocument::Compact));
}

void VoiceClient::sendEnvSnapshot(const QVariantMap &data)
{
    if (data.isEmpty() || !m_handshakeDone) {
        return;
    }

    QJsonObject obj;
    obj.insert(QStringLiteral("type"), QStringLiteral("env_snapshot"));
    obj.insert(QStringLiteral("data"), QJsonObject::fromVariantMap(data));
    sendText(QJsonDocument(obj).toJson(QJsonDocument::Compact));
}

void VoiceClient::suspendAudioForCall()
{
    m_pendingStart = false;
    if (m_recording) {
        endAudioInput();
        if (m_handshakeDone) {
            sendText(QByteArrayLiteral("{\"type\":\"stop\"}"));
        }
    } else {
        endAudioInput();
    }
    clearAudioOutput();
    m_waitingForReply = false;
}

void VoiceClient::resumeAudioAfterCall()
{
    m_pendingStart = false;
    m_waitingForReply = false;
}

void VoiceClient::onConnected()
{
    m_socket->write(buildHandshake());
}

void VoiceClient::onReadyRead()
{
    m_buffer.append(m_socket->readAll());
    if (!m_handshakeDone) {
        processHandshake();
    }
    if (m_handshakeDone) {
        processFrames();
    }
}

void VoiceClient::onDisconnected()
{
    m_handshakeDone = false;
    m_pendingStart = false;
    m_waitingForReply = false;
    endAudioInput();
    const bool normalClose = m_closingAfterComplete;
    m_closingAfterComplete = false;
    if (normalClose || !m_audioPlaying) {
        clearAudioOutput();
    }
    if (!normalClose) {
        emit connectedChanged(false);
    }
}

void VoiceClient::sendText(const QByteArray &payload)
{
    sendFrame(0x1, payload);
}

void VoiceClient::sendBinary(const QByteArray &payload)
{
    sendFrame(0x2, payload);
}

void VoiceClient::sendFrame(quint8 opcode, const QByteArray &payload)
{
    if (!m_handshakeDone || m_socket->state() != QAbstractSocket::ConnectedState) {
        return;
    }

    QByteArray frame;
    frame.append(static_cast<char>(0x80 | opcode));
    const qsizetype size = payload.size();
    if (size < 126) {
        frame.append(static_cast<char>(0x80 | size));
    } else if (size <= 0xFFFF) {
        frame.append(static_cast<char>(0x80 | 126));
        frame.append(static_cast<char>((size >> 8) & 0xFF));
        frame.append(static_cast<char>(size & 0xFF));
    } else {
        frame.append(static_cast<char>(0x80 | 127));
        for (int i = 7; i >= 0; --i) {
            frame.append(static_cast<char>((static_cast<quint64>(size) >> (8 * i)) & 0xFF));
        }
    }

    const QByteArray mask = randomBytes(4);
    frame.append(mask);
    QByteArray masked = payload;
    for (int i = 0; i < masked.size(); ++i) {
        masked[i] = static_cast<char>(masked[i] ^ mask.at(i % 4));
    }
    frame.append(masked);
    m_socket->write(frame);
}

void VoiceClient::processHandshake()
{
    const int end = m_buffer.indexOf("\r\n\r\n");
    if (end < 0) {
        return;
    }

    const QByteArray header = m_buffer.left(end);
    m_buffer.remove(0, end + 4);
    if (!header.startsWith("HTTP/1.1 101") && !header.startsWith("HTTP/1.0 101")) {
        emit errorText(QString::fromUtf8(header));
        m_socket->disconnectFromHost();
        return;
    }

    m_handshakeDone = true;
    emit connectedChanged(true);
    if (m_pendingStart) {
        beginAudioInput();
    }
}

void VoiceClient::processFrames()
{
    while (m_buffer.size() >= 2) {
        const quint8 b0 = static_cast<quint8>(m_buffer[0]);
        const quint8 b1 = static_cast<quint8>(m_buffer[1]);
        quint64 len = b1 & 0x7F;
        int offset = 2;
        if (len == 126) {
            if (m_buffer.size() < offset + 2) {
                return;
            }
            len = (static_cast<quint8>(m_buffer[offset]) << 8) | static_cast<quint8>(m_buffer[offset + 1]);
            offset += 2;
        } else if (len == 127) {
            if (m_buffer.size() < offset + 8) {
                return;
            }
            len = 0;
            for (int i = 0; i < 8; ++i) {
                len = (len << 8) | static_cast<quint8>(m_buffer[offset + i]);
            }
            offset += 8;
        }

        const bool masked = (b1 & 0x80) != 0;
        QByteArray mask;
        if (masked) {
            if (m_buffer.size() < offset + 4) {
                return;
            }
            mask = m_buffer.mid(offset, 4);
            offset += 4;
        }
        if (len > static_cast<quint64>(m_buffer.size() - offset)) {
            return;
        }

        QByteArray payload = m_buffer.mid(offset, static_cast<int>(len));
        m_buffer.remove(0, offset + static_cast<int>(len));
        if (masked) {
            for (int i = 0; i < payload.size(); ++i) {
                payload[i] = static_cast<char>(payload[i] ^ mask.at(i % 4));
            }
        }

        const quint8 opcode = b0 & 0x0F;
        if (opcode == 0x1) {
            handleTextMessage(payload);
        } else if (opcode == 0x2) {
            handleBinaryMessage(payload);
        } else if (opcode == 0x8) {
            m_socket->disconnectFromHost();
            return;
        }
    }
}

void VoiceClient::handleTextMessage(const QByteArray &payload)
{
    const QJsonDocument doc = QJsonDocument::fromJson(payload);
    if (!doc.isObject()) {
        return;
    }
    const QJsonObject obj = doc.object();
    const QString type = obj.value(QStringLiteral("type")).toString();
    const QString text = obj.value(QStringLiteral("text")).toString();

    if (type == QStringLiteral("state")) {
        const QString state = obj.value(QStringLiteral("state")).toString();
        if (state == QStringLiteral("ai_thinking") || state == QStringLiteral("ai_speaking")) {
            m_waitingForReply = true;
        } else if (state == QStringLiteral("idle")) {
            m_waitingForReply = false;
        } else if (state == QStringLiteral("user_interrupting")) {
            m_waitingForReply = false;
            clearAudioOutput();
        }
        emit stateChanged(state);
    } else if (type == QStringLiteral("asr_partial")) {
        emit asrPartial(text);
    } else if (type == QStringLiteral("asr_final")) {
        emit asrFinal(text);
    } else if (type == QStringLiteral("llm_stream_start")) {
        clearAudioOutput();
        m_streamingText.clear();
    } else if (type == QStringLiteral("llm_delta")) {
        const bool firstToken = m_streamingText.isEmpty() && !text.isEmpty();
        if (text.startsWith(m_streamingText)) {
            m_streamingText = text;
        } else {
            m_streamingText += text;
        }
        if (firstToken) {
            emit streamingStarted();
        }
        emit replyPartial(m_streamingText);
    } else if (type == QStringLiteral("llm_reply") || type == QStringLiteral("llm_stream_end")) {
        m_waitingForReply = false;
        const QString fullText = m_streamingText.isEmpty() ? text : m_streamingText;
        emit streamingFinished(fullText);
        emit replyText(fullText);
        m_streamingText.clear();
    } else if (type == QStringLiteral("map_tool_call")) {
        m_waitingForReply = false;
        m_streamingText.clear();
        emit mapToolCall(text, obj.value(QStringLiteral("command")).toString());
    } else if (type == QStringLiteral("tts_error")) {
        emit ttsError(obj.value(QStringLiteral("message")).toString());
    } else if (type == QStringLiteral("stm32_command")) {
        const QString command = obj.value(QStringLiteral("command")).toString();
        if (!command.trimmed().isEmpty()) {
            emit stm32CommandReceived(command.toUtf8());
        }
    } else if (type == QStringLiteral("control") && obj.value(QStringLiteral("command")).toString() == QStringLiteral("stop")) {
        m_waitingForReply = false;
        clearAudioOutput();
    } else if (type == QStringLiteral("error")) {
        m_waitingForReply = false;
        emit errorText(obj.value(QStringLiteral("message")).toString());
        m_socket->disconnectFromHost();
    }
}

void VoiceClient::handleBinaryMessage(const QByteArray &payload)
{
    if (payload.size() < 8 || payload.left(4) != QByteArrayLiteral("TTS1")) {
        return;
    }

    const auto *raw = reinterpret_cast<const uchar *>(payload.constData() + 4);
    const int sampleRate = int(raw[0]) | (int(raw[1]) << 8) | (int(raw[2]) << 16) | (int(raw[3]) << 24);
    const QByteArray pcm = payload.mid(8);
    if (m_audioSink && (m_audioSink->state() == QAudio::IdleState || m_audioSink->state() == QAudio::StoppedState)) {
        clearAudioOutput();
    }
    ensureAudioOutput(sampleRate);
    if (m_audioSink && m_audioOutput) {
        m_playbackIdleTimer->stop();
        const QByteArray outputPcm = convertPcm16MonoForOutput(pcm, sampleRate, m_outputSampleRate, m_outputChannels);
        if (outputPcm.isEmpty()) {
            return;
        }
        const int bytesPerSecond = qMax(1, m_outputSampleRate * m_outputChannels * 2);
        m_pendingPlaybackMs += int((qint64(outputPcm.size()) * 1000 + bytesPerSecond - 1) / bytesPerSecond);
        m_playbackBuffer.append(outputPcm);
        setAudioPlaying(true);
        flushAudioOutput();
    }
}

void VoiceClient::beginAudioInput()
{
    if (m_recording) {
        return;
    }

    m_recordingPcm.clear();
    m_sentBytes = 0;

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
    QAudioFormat format = pcm16Format(kInputSampleRate);
    if (!inputDevice.isFormatSupported(format)) {
        format = inputDevice.preferredFormat();
        if (!isPcm16Format(format)) {
            emit errorText(QStringLiteral("默认麦克风不支持 Int16 PCM 录音"));
            return;
        }
        if (format.channelCount() != 1) {
            QAudioFormat monoFormat = format;
            monoFormat.setChannelCount(1);
            if (inputDevice.isFormatSupported(monoFormat)) {
                format = monoFormat;
            }
        }
    }
#else
    QAudioFormat format = inputDevice.preferredFormat();
    if (!isPcm16Format(format) || format.sampleRate() <= 0 || format.channelCount() <= 0) {
        format = inputDevice.nearestFormat(pcm16Format(kInputSampleRate));
    }
    if (!isPcm16Format(format) || format.sampleRate() <= 0 || format.channelCount() <= 0) {
        emit errorText(QStringLiteral("默认麦克风不支持 Int16 PCM 录音"));
        return;
    }
#endif

    m_captureSampleRate = format.sampleRate();
    m_captureChannels = qMax(1, format.channelCount());
    m_inputSampleRate = kInputSampleRate;
#if QT_VERSION >= QT_VERSION_CHECK(6, 0, 0)
    m_audioSource = new QAudioSource(inputDevice, format, this);
#else
    m_audioSource = new QAudioInput(inputDevice, format, this);
#endif
    m_audioSource->setBufferSize(3200);
#if QT_VERSION >= QT_VERSION_CHECK(6, 0, 0)
    connect(m_audioSource, &QAudioSource::stateChanged, this, [this](QAudio::State state) {
#else
    connect(m_audioSource, &QAudioInput::stateChanged, this, [this](QAudio::State state) {
#endif
        if (state == QAudio::StoppedState && m_recording && m_audioSource && m_audioSource->error() != QAudio::NoError) {
            emit errorText(QStringLiteral("麦克风采集停止，错误码：%1").arg(int(m_audioSource->error())));
        }
    });

    m_audioInput = m_audioSource->start();
    if (!m_audioInput) {
        emit errorText(QStringLiteral("无法打开麦克风"));
        m_audioSource->deleteLater();
        m_audioSource = nullptr;
        return;
    }

    connect(m_audioInput, &QIODevice::readyRead, this, [this]() {
        const QByteArray capturedPcm = m_audioInput->readAll();
        const QByteArray pcm = normalizePcm16Mono16k(capturedPcm, m_captureSampleRate, m_captureChannels);
        if (!pcm.isEmpty()) {
            m_recordingPcm.append(pcm);
            m_sentBytes += pcm.size();
            emit audioInputStats(peakPercent(pcm), m_sentBytes);
            sendBinary(buildPcmFrame(pcm));
        }
    });

    m_recording = true;
    m_pendingStart = false;
    emit recordingChanged(true);

}

void VoiceClient::endAudioInput()
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
    if (m_recording) {
        saveRecording();
        m_recording = false;
        emit recordingChanged(false);
    }
}

void VoiceClient::ensureAudioOutput(int sampleRate)
{
    if (sampleRate <= 0) {
        return;
    }
    if (m_audioSink && m_ttsInputSampleRate == sampleRate) {
        return;
    }

    clearAudioOutput();
    m_ttsInputSampleRate = sampleRate;

#if QT_VERSION >= QT_VERSION_CHECK(6, 0, 0)
    const QAudioDevice outputDevice = QMediaDevices::defaultAudioOutput();
#else
    const QAudioDeviceInfo outputDevice = chooseQt5AudioDevice(QAudio::AudioOutput, QAudioDeviceInfo::defaultOutputDevice(), "output");
#endif
    if (outputDevice.isNull()) {
        emit ttsError(QStringLiteral("没有可用音频输出设备"));
        return;
    }

    QAudioFormat format = outputDevice.preferredFormat();
    if (!isPcm16Format(format) || format.sampleRate() <= 0 || format.channelCount() <= 0) {
        format = pcm16Format(sampleRate);
    }

    if (!outputDevice.isFormatSupported(format)) {
        format = pcm16Format(sampleRate);
        QAudioFormat stereoFormat = format;
        stereoFormat.setChannelCount(2);
        if (outputDevice.isFormatSupported(stereoFormat)) {
            format = stereoFormat;
        } else {
            const QAudioFormat preferred = outputDevice.preferredFormat();
            if (!isPcm16Format(preferred) || preferred.sampleRate() <= 0 || preferred.channelCount() <= 0) {
                emit ttsError(QStringLiteral("默认音频输出不支持 Int16 PCM 播放"));
                return;
            }
            format = preferred;
        }
    }

    m_outputSampleRate = format.sampleRate();
    m_outputChannels = qMax(1, format.channelCount());
#if QT_VERSION >= QT_VERSION_CHECK(6, 0, 0)
    m_audioSink = new QAudioSink(outputDevice, format, this);
#else
    m_audioSink = new QAudioOutput(outputDevice, format, this);
#endif
    m_audioSink->setBufferSize(m_outputSampleRate * m_outputChannels * 2);
#if QT_VERSION >= QT_VERSION_CHECK(6, 0, 0)
    connect(m_audioSink, &QAudioSink::stateChanged, this, [this](QAudio::State state) {
#else
    connect(m_audioSink, &QAudioOutput::stateChanged, this, [this](QAudio::State state) {
#endif
        if ((state == QAudio::IdleState || state == QAudio::StoppedState)
            && m_playbackBuffer.isEmpty()
            && !m_playbackIdleTimer->isActive()) {
            m_playbackIdleTimer->setInterval(200);
            m_playbackIdleTimer->start();
        }
    });
    m_audioOutput = m_audioSink->start();
    if (!m_audioOutput) {
        emit ttsError(QStringLiteral("无法打开音频输出设备"));
        clearAudioOutput();
    }
}

void VoiceClient::flushAudioOutput()
{
    if (!m_audioSink || !m_audioOutput || m_playbackBuffer.isEmpty()) {
        m_playbackFlushTimer->stop();
        return;
    }

    if (m_audioSink->state() == QAudio::StoppedState) {
        m_audioOutput = m_audioSink->start();
        if (!m_audioOutput) {
            emit ttsError(QStringLiteral("音频输出重新启动失败"));
            m_playbackFlushTimer->stop();
            setAudioPlaying(false);
            return;
        }
    }

    while (!m_playbackBuffer.isEmpty() && m_audioSink->bytesFree() > 0) {
        const qsizetype chunkSize = qMin<qsizetype>(m_playbackBuffer.size(), m_audioSink->bytesFree());
        const qint64 written = m_audioOutput->write(m_playbackBuffer.constData(), chunkSize);
        if (written <= 0) {
            break;
        }
        m_playbackBuffer.remove(0, int(written));
    }

    if (m_playbackBuffer.isEmpty()) {
        m_playbackFlushTimer->stop();
        if (m_audioPlaying && !m_playbackIdleTimer->isActive()) {
            m_playbackIdleTimer->setInterval(qMax(600, m_pendingPlaybackMs + 300));
            m_pendingPlaybackMs = 0;
            m_playbackIdleTimer->start();
        }
    } else if (!m_playbackFlushTimer->isActive()) {
        m_playbackFlushTimer->start();
    }
}

void VoiceClient::clearAudioOutput()
{
    m_playbackIdleTimer->stop();
    m_playbackFlushTimer->stop();
    m_playbackBuffer.clear();
    m_pendingPlaybackMs = 0;
    if (m_audioSink) {
        setAudioPlaying(false);
        disconnect(m_audioSink, nullptr, this, nullptr);
        m_audioSink->stop();
        m_audioSink->deleteLater();
        m_audioSink = nullptr;
        m_audioOutput = nullptr;
        m_ttsInputSampleRate = 0;
        m_outputSampleRate = 0;
        m_outputChannels = 1;
    }
}

void VoiceClient::setAudioPlaying(bool playing)
{
    if (m_audioPlaying == playing) {
        return;
    }
    m_audioPlaying = playing;
    if (m_handshakeDone) {
        sendText(playing ? QByteArrayLiteral("{\"type\":\"tts_start\"}")
                         : QByteArrayLiteral("{\"type\":\"tts_stop\"}"));
    }
    emit audioPlayingChanged(playing);
}

QByteArray VoiceClient::buildPcmFrame(const QByteArray &pcm) const
{
    QByteArray frame;
    frame.reserve(12 + pcm.size());
    frame.append("PCM2", 4);
    appendLE32(frame, quint32(m_inputSampleRate));
    appendLE32(frame, m_audioPlaying ? 1u : 0u);
    frame.append(pcm);
    return frame;
}

void VoiceClient::saveRecording()
{
    if (m_recordingPcm.isEmpty()) {
        return;
    }

    QString dirPath = QStandardPaths::writableLocation(QStandardPaths::TempLocation);
    if (dirPath.isEmpty()) {
        dirPath = QDir::tempPath();
    }
    QDir dir(dirPath);
    const QString fileName = QStringLiteral("qianrushi_last_voice_input_%1.wav")
        .arg(QDateTime::currentDateTime().toString(QStringLiteral("yyyyMMdd_hhmmss")));
    const QString path = dir.filePath(fileName);

    QByteArray wav;
    const quint32 dataSize = quint32(m_recordingPcm.size());
    wav.append("RIFF", 4);
    appendLE32(wav, 36 + dataSize);
    wav.append("WAVE", 4);
    wav.append("fmt ", 4);
    appendLE32(wav, 16);
    appendLE16(wav, 1);
    appendLE16(wav, 1);
    appendLE32(wav, quint32(m_inputSampleRate));
    appendLE32(wav, quint32(m_inputSampleRate * 2));
    appendLE16(wav, 2);
    appendLE16(wav, 16);
    wav.append("data", 4);
    appendLE32(wav, dataSize);
    wav.append(m_recordingPcm);

    QFile file(path);
    if (file.open(QIODevice::WriteOnly)) {
        file.write(wav);
        file.close();
        emit recordingSaved(path);
    }
}

QByteArray VoiceClient::buildHandshake() const
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
