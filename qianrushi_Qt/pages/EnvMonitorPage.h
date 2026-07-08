#pragma once

#include <QFrame>
#include <QWidget>
#include <QList>
#include <QMap>
#include <QTimer>
#include <QTcpSocket>
#include <QAbstractSocket>
#include <QUrl>
#include <QVariantMap>

class NavigationController;
class SerialEnvReader;
class QLabel;
class QGridLayout;
class QResizeEvent;

class EnvDataCard : public QFrame
{
    Q_OBJECT
public:
    explicit EnvDataCard(const QString &title, const QString &unit, QWidget *parent = nullptr);
    void updateValue(const QString &value);

private:
    QLabel *m_title = nullptr;
    QLabel *m_value = nullptr;
    QLabel *m_unit = nullptr;
    QLabel *m_status = nullptr;
};

class EnvMonitorPage : public QWidget
{
    Q_OBJECT
public:
    explicit EnvMonitorPage(NavigationController *nav, QWidget *parent = nullptr);
    void refresh();

public slots:
    void writeSerialCommand(const QByteArray &payload);

protected:
    void resizeEvent(QResizeEvent *event) override;

signals:
    void environmentDataUpdated(const QVariantMap &data);

private slots:
    void onConnected();
    void onReadyRead();
    void onDisconnected();
    void onReconnectTimer();
    void onSerialStatusChanged(bool online, const QString &message);
    void sendRawSerialJsonToBackend(const QByteArray &payload);

private:
    void setupUI();
    void connectToBackend();
    void connectToSerial();
    void processHandshake();
    void processFrames();
    void sendTextFrame(const QByteArray &payload);
    void processMessage(const QByteArray &msg);
    void updateEnvData(const QVariantMap &data);
    void showAlarm(bool active, const QString &message);
    void showCommand(const QString &message);
    void arrangeCards();
    void updateStatusLine();

    NavigationController *m_nav = nullptr;
    QGridLayout *m_grid = nullptr;
    QLabel *m_footer = nullptr;
    QLabel *m_alarmBanner = nullptr;

    QTcpSocket *m_socket = nullptr;
    QTimer *m_reconnectTimer = nullptr;
    QUrl m_envUrl;
    SerialEnvReader *m_serialReader = nullptr;
    QByteArray m_buffer;
    bool m_connected = false;
    bool m_handshakeDone = false;
    bool m_serialOnline = false;
    QString m_serialMessage;
    QString m_deviceName = QStringLiteral("Light / SensorH7");
    QString m_deviceStatus = QStringLiteral("offline");
    QString m_lastUpdate = QStringLiteral("--:--:--");

    QMap<QString, EnvDataCard *> m_cards;
    QList<EnvDataCard *> m_cardOrder;

    struct DisplayField {
        QString key;
        QString title;
        QString unit;
    };
    QList<DisplayField> m_fields;
};
