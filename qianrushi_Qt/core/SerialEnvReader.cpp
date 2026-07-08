#include "SerialEnvReader.h"

#include <QByteArray>
#include <QJsonDocument>
#include <QJsonObject>
#include <QJsonParseError>
#include <QJsonValue>
#include <QIODevice>
#include <QSerialPort>
#include <QStringList>
#include <QtGlobal>
#include <QVariantMap>
#include <QDebug>

namespace {
constexpr int kDefaultBaudRate = 115200;
constexpr int kMaxBufferSize = 64 * 1024;
const char kJsonPrefix[] = "JSON:";
const QByteArray kUtf8Bom = QByteArray::fromHex("EFBBBF");

const QStringList kNumericFields = {
    QStringLiteral("Enviroment_Temperation"),
    QStringLiteral("Enviroment_Humidity"),
    QStringLiteral("Enviroment_Light"),
    QStringLiteral("Enviroment_Pm25"),
    QStringLiteral("Enviroment_Pm10"),
    QStringLiteral("Wind_Speed"),
    QStringLiteral("Wind_Direction"),
};
}

SerialEnvReader::SerialEnvReader(QObject *parent)
    : QObject(parent)
    , m_serial(new QSerialPort(this))
    , m_portName(configuredPortName())
    , m_baudRate(configuredBaudRate())
{
    connect(m_serial, &QSerialPort::readyRead, this, &SerialEnvReader::onReadyRead);
    connect(m_serial, &QSerialPort::errorOccurred, this, &SerialEnvReader::onErrorOccurred);
}

void SerialEnvReader::start()
{
    if (m_serial->isOpen()) {
        return;
    }

    m_buffer.clear();
    m_serial->setPortName(m_portName);
    m_serial->setBaudRate(m_baudRate);
    m_serial->setDataBits(QSerialPort::Data8);
    m_serial->setParity(QSerialPort::NoParity);
    m_serial->setStopBits(QSerialPort::OneStop);
    m_serial->setFlowControl(QSerialPort::NoFlowControl);

    if (!m_serial->open(QIODevice::ReadWrite)) {
        const QString message = QStringLiteral("Serial open failed: error=%1").arg(int(m_serial->error()));
        qWarning().noquote() << "SerialEnvReader" << message;
        emit statusChanged(false, message);
        return;
    }

    const QString message = QStringLiteral("Serial connected: %1 @ %2").arg(m_portName).arg(m_baudRate);
    qDebug().noquote() << "SerialEnvReader" << message;
    emit statusChanged(true, message);
}

void SerialEnvReader::stop()
{
    if (m_serial->isOpen()) {
        m_serial->close();
    }
    m_buffer.clear();
    emit statusChanged(false, QStringLiteral("Serial disconnected"));
}

bool SerialEnvReader::isOpen() const
{
    return m_serial->isOpen();
}

bool SerialEnvReader::writeCommand(const QByteArray &payload)
{
    if (payload.trimmed().isEmpty()) {
        qWarning() << "SerialEnvReader ignored empty command payload";
        return false;
    }
    if (!m_serial->isOpen()) {
        start();
    }
    if (!m_serial->isOpen()) {
        qWarning() << "SerialEnvReader command write failed: serial not open";
        return false;
    }

    QByteArray frame = payload;
    if (!frame.endsWith('\n')) {
        frame.append('\n');
    }
    const qint64 written = m_serial->write(frame);
    if (written != frame.size()) {
        qWarning() << "SerialEnvReader command write incomplete" << written << frame.size();
        return false;
    }
    if (!m_serial->flush()) {
        qWarning() << "SerialEnvReader command flush failed";
        return false;
    }
    if (!m_serial->waitForBytesWritten(300)) {
        qWarning() << "SerialEnvReader command write timeout";
        return false;
    }
    qDebug() << "SerialEnvReader command forwarded to STM32, bytes=" << frame.size();
    return true;
}

QString SerialEnvReader::portName() const
{
    return m_portName;
}

int SerialEnvReader::baudRate() const
{
    return m_baudRate;
}

void SerialEnvReader::onReadyRead()
{
    m_buffer.append(m_serial->readAll());
    if (m_buffer.size() > kMaxBufferSize) {
        qWarning() << "SerialEnvReader buffer overflow, dropping buffered serial data";
        m_buffer.clear();
        return;
    }

    while (true) {
        const int newline = m_buffer.indexOf('\n');
        if (newline < 0) {
            return;
        }
        QByteArray line = m_buffer.left(newline);
        m_buffer.remove(0, newline + 1);
        processLine(line.trimmed());
    }
}

void SerialEnvReader::onErrorOccurred()
{
    if (m_serial->error() == QSerialPort::NoError) {
        return;
    }
    const QString message = QStringLiteral("Serial error: error=%1").arg(int(m_serial->error()));
    qWarning().noquote() << "SerialEnvReader" << message;
    emit statusChanged(false, message);
}

void SerialEnvReader::processLine(const QByteArray &line)
{
    if (line.isEmpty()) {
        return;
    }

    QByteArray payload = line.trimmed();
    if (payload.startsWith(kUtf8Bom)) {
        payload = payload.mid(kUtf8Bom.size()).trimmed();
    }

    const QByteArray lower = payload.left(int(sizeof(kJsonPrefix) - 1)).toLower();
    if (lower == QByteArrayLiteral("json:")) {
        payload = payload.mid(int(sizeof(kJsonPrefix) - 1)).trimmed();
        if (payload.startsWith(kUtf8Bom)) {
            payload = payload.mid(kUtf8Bom.size()).trimmed();
        }
    } else if (!payload.startsWith('{')) {
        const int brace = payload.indexOf('{');
        if (brace >= 0) {
            qDebug() << "SerialEnvReader found JSON object after serial prefix; STM32 JSON lines should end with newline";
            payload = payload.mid(brace).trimmed();
        } else {
            static int ignoredLineCount = 0;
            if ((++ignoredLineCount % 50) == 1) {
                qDebug() << "SerialEnvReader ignored non-JSON serial line; STM32 JSON lines must end with newline";
            }
            return;
        }
    }

    parseJsonPayload(payload);
}

void SerialEnvReader::parseJsonPayload(const QByteArray &payload)
{
    QJsonParseError err;
    const QJsonDocument doc = QJsonDocument::fromJson(payload, &err);
    if (err.error != QJsonParseError::NoError || !doc.isObject()) {
        qWarning() << "SerialEnvReader JSON parse failed" << err.errorString();
        return;
    }

    const QJsonObject obj = doc.object();
    emit rawJsonReady(payload.trimmed());

    QVariantMap data;
    for (const QString &key : kNumericFields) {
        if (!obj.contains(key) || obj.value(key).isNull() || obj.value(key).isUndefined()) {
            continue;
        }
        const QJsonValue value = obj.value(key);
        if (!value.isDouble()) {
            qWarning().noquote() << "SerialEnvReader invalid field type:" << key;
            continue;
        }
        data.insert(key, value.toDouble());
    }

    if (!data.isEmpty()) {
        emit envDataReady(data);
    }
}

QString SerialEnvReader::configuredPortName()
{
    const QString value = QString::fromLocal8Bit(qgetenv("SMARTNAV_ENV_SERIAL")).trimmed();
    if (value.isEmpty()) {
        return QStringLiteral("/dev/ttySTM32");
    }
    return value;
}

int SerialEnvReader::configuredBaudRate()
{
    bool ok = false;
    const QByteArray raw = qgetenv("SMARTNAV_ENV_BAUD").trimmed();
    const int value = raw.toInt(&ok);
    if (!ok || value <= 0) {
        return kDefaultBaudRate;
    }
    return value;
}
