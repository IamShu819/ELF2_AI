#pragma once

#include <QByteArray>
#include <QObject>
#include <QString>
#include <QVariantMap>

class QSerialPort;

class SerialEnvReader : public QObject
{
    Q_OBJECT

public:
    explicit SerialEnvReader(QObject *parent = nullptr);

    void start();
    void stop();
    bool isOpen() const;
    bool writeCommand(const QByteArray &payload);
    QString portName() const;
    int baudRate() const;

signals:
    void envDataReady(const QVariantMap &data);
    void rawJsonReady(const QByteArray &payload);
    void statusChanged(bool online, const QString &message);

private slots:
    void onReadyRead();
    void onErrorOccurred();

private:
    void processLine(const QByteArray &line);
    void parseJsonPayload(const QByteArray &payload);
    static QString configuredPortName();
    static int configuredBaudRate();

    QSerialPort *m_serial = nullptr;
    QByteArray m_buffer;
    QString m_portName;
    int m_baudRate = 115200;
};
