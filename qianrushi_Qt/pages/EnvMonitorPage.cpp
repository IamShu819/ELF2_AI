#include "EnvMonitorPage.h"
#include "core/BackendConfig.h"
#include "core/NavigationController.h"
#include "core/SerialEnvReader.h"

#include <QFrame>
#include <QGridLayout>
#include <QHBoxLayout>
#include <QJsonArray>
#include <QJsonDocument>
#include <QJsonObject>
#include <QLabel>
#include <QDebug>
#include <QLayoutItem>
#include <QMetaType>
#include <QResizeEvent>
#include <QRandomGenerator>
#include <QScrollArea>
#include <QSizePolicy>
#include <QStyle>
#include <QTime>
#include <QVBoxLayout>

/* ---- EnvDataCard ---- */

EnvDataCard::EnvDataCard(const QString &title, const QString &unit, QWidget *parent)
    : QFrame(parent)
{
    setObjectName(QStringLiteral("EnvDataCard"));
    setMinimumSize(210, 146);
    setSizePolicy(QSizePolicy::Expanding, QSizePolicy::Preferred);

    auto *layout = new QVBoxLayout(this);
    layout->setContentsMargins(20, 16, 20, 16);
    layout->setSpacing(10);

    auto *topRow = new QHBoxLayout;
    topRow->setContentsMargins(0, 0, 0, 0);
    topRow->setSpacing(8);

    m_title = new QLabel(title, this);
    m_title->setObjectName(QStringLiteral("EnvCardTitle"));
    m_title->setAlignment(Qt::AlignLeft | Qt::AlignVCenter);

    m_unit = new QLabel(unit, this);
    m_unit->setObjectName(QStringLiteral("EnvCardUnit"));
    m_unit->setAlignment(Qt::AlignRight | Qt::AlignVCenter);

    topRow->addWidget(m_title, 1);
    topRow->addWidget(m_unit, 0);

    m_value = new QLabel(QStringLiteral("--"), this);
    m_value->setObjectName(QStringLiteral("EnvCardValue"));
    m_value->setAlignment(Qt::AlignLeft | Qt::AlignVCenter);
    m_value->setMinimumHeight(54);

    m_status = new QLabel(QStringLiteral("--"), this);
    m_status->setObjectName(QStringLiteral("EnvCardHint"));
    m_status->setAlignment(Qt::AlignLeft | Qt::AlignVCenter);

    layout->addLayout(topRow);
    layout->addWidget(m_value, 1);
    layout->addWidget(m_status);
}

void EnvDataCard::updateValue(const QString &value)
{
    m_value->setText(value);
    if (value == QStringLiteral("--")) {
        m_status->setText(QStringLiteral("无数据"));
    } else {
        m_status->setText(QStringLiteral("正常"));
    }
}

/* ---- EnvMonitorPage ---- */

EnvMonitorPage::EnvMonitorPage(NavigationController *nav, QWidget *parent)
    : QWidget(parent)
    , m_nav(nav)
    , m_envUrl(BackendConfig::envUrl())
{
    m_fields = {
        {QStringLiteral("Enviroment_Temperation"), QStringLiteral("环境温度"), QStringLiteral("℃")},
        {QStringLiteral("Enviroment_Humidity"),    QStringLiteral("空气湿度"), QStringLiteral("%")},
        {QStringLiteral("Enviroment_Light"),       QStringLiteral("光照强度"), QStringLiteral("Lux")},
        {QStringLiteral("Enviroment_Pm25"),        QStringLiteral("PM 2.5"),  QStringLiteral("ug")},
        {QStringLiteral("Enviroment_Pm10"),        QStringLiteral("PM 10"),   QStringLiteral("ug")},
        {QStringLiteral("Wind_Speed"),             QStringLiteral("风速"),     QStringLiteral("m/s")},
        {QStringLiteral("Wind_Direction"),         QStringLiteral("风向"),     QStringLiteral("°")},
    };

    setupUI();
    connectToSerial();
    connectToBackend();
}

void EnvMonitorPage::setupUI()
{
    auto *rootLayout = new QVBoxLayout(this);
    rootLayout->setContentsMargins(24, 14, 24, 20);
    rootLayout->setSpacing(14);

    /* 页面概览条 */
    auto *summaryFrame = new QFrame(this);
    summaryFrame->setObjectName(QStringLiteral("EnvSummary"));
    auto *summaryLayout = new QHBoxLayout(summaryFrame);
    summaryLayout->setContentsMargins(22, 16, 22, 16);
    summaryLayout->setSpacing(18);

    auto *titleBox = new QVBoxLayout;
    titleBox->setContentsMargins(0, 0, 0, 0);
    titleBox->setSpacing(6);
    auto *title = new QLabel(QStringLiteral("环境监测"), summaryFrame);
    title->setObjectName(QStringLiteral("EnvPageTitle"));
    auto *subtitle = new QLabel(QStringLiteral("实时查看温湿度、空气质量、光照与风速数据"), summaryFrame);
    subtitle->setObjectName(QStringLiteral("EnvPageSubtitle"));
    subtitle->setWordWrap(true);
    titleBox->addWidget(title);
    titleBox->addWidget(subtitle);

    m_footer = new QLabel(QStringLiteral("串口：-- | 波特率：-- | 设备：Light / SensorH7 | 状态：offline | 更新时间：--:--:--"), summaryFrame);
    m_footer->setObjectName(QStringLiteral("EnvStatusPill"));
    m_footer->setAlignment(Qt::AlignCenter);
    m_footer->setMinimumHeight(36);
    m_footer->setMinimumWidth(230);

    summaryLayout->addLayout(titleBox, 1);
    summaryLayout->addWidget(m_footer, 0, Qt::AlignRight | Qt::AlignVCenter);
    rootLayout->addWidget(summaryFrame);

    /* 报警横幅（默认隐藏） */
    m_alarmBanner = new QLabel(this);
    m_alarmBanner->setObjectName(QStringLiteral("EnvAlarmBanner"));
    m_alarmBanner->setAlignment(Qt::AlignLeft | Qt::AlignVCenter);
    m_alarmBanner->setWordWrap(true);
    m_alarmBanner->setMinimumHeight(46);
    m_alarmBanner->hide();
    rootLayout->addWidget(m_alarmBanner);

    /* 数据卡片网格 */
    auto *gridContainer = new QWidget(this);
    gridContainer->setObjectName(QStringLiteral("EnvGridContainer"));
    m_grid = new QGridLayout(gridContainer);
    m_grid->setHorizontalSpacing(16);
    m_grid->setVerticalSpacing(16);
    m_grid->setContentsMargins(0, 0, 0, 0);

    for (int i = 0; i < m_fields.size(); ++i) {
        const auto &f = m_fields.at(i);
        auto *card = new EnvDataCard(f.title, f.unit, gridContainer);
        m_cards[f.key] = card;
        m_cardOrder.append(card);
    }
    arrangeCards();

    auto *scrollArea = new QScrollArea(this);
    scrollArea->setObjectName(QStringLiteral("EnvGridScrollArea"));
    scrollArea->setWidgetResizable(true);
    scrollArea->setFrameShape(QFrame::NoFrame);
    scrollArea->setHorizontalScrollBarPolicy(Qt::ScrollBarAlwaysOff);
    scrollArea->setVerticalScrollBarPolicy(Qt::ScrollBarAsNeeded);
    scrollArea->setWidget(gridContainer);

    rootLayout->addWidget(scrollArea, 1);
}

void EnvMonitorPage::writeSerialCommand(const QByteArray &payload)
{
    if (!m_serialReader) {
        return;
    }
    m_serialReader->writeCommand(payload);
}

void EnvMonitorPage::connectToSerial()
{
    m_serialReader = new SerialEnvReader(this);
    connect(m_serialReader, &SerialEnvReader::envDataReady, this, &EnvMonitorPage::updateEnvData);
    connect(m_serialReader, &SerialEnvReader::rawJsonReady, this, &EnvMonitorPage::sendRawSerialJsonToBackend);
    connect(m_serialReader, &SerialEnvReader::statusChanged, this, &EnvMonitorPage::onSerialStatusChanged);
    m_serialReader->start();
}

void EnvMonitorPage::connectToBackend()
{
    m_socket = new QTcpSocket(this);
    connect(m_socket, &QTcpSocket::connected, this, &EnvMonitorPage::onConnected);
    connect(m_socket, &QTcpSocket::readyRead, this, &EnvMonitorPage::onReadyRead);
    connect(m_socket, &QTcpSocket::disconnected, this, &EnvMonitorPage::onDisconnected);

    m_reconnectTimer = new QTimer(this);
    m_reconnectTimer->setSingleShot(true);
    connect(m_reconnectTimer, &QTimer::timeout, this, &EnvMonitorPage::onReconnectTimer);

    /* 延迟连接，等待 main window 初始化完成 */
    QTimer::singleShot(500, this, &EnvMonitorPage::onReconnectTimer);
}

void EnvMonitorPage::onSerialStatusChanged(bool online, const QString &message)
{
    m_serialOnline = online;
    m_serialMessage = message;
    m_deviceStatus = online ? QStringLiteral("online") : QStringLiteral("offline");
    updateStatusLine();
}

void EnvMonitorPage::onConnected()
{
    m_connected = true;
    m_handshakeDone = false;
    m_buffer.clear();
    updateStatusLine();

    /* 发送 WebSocket 握手 */
    const QByteArray key = "dGhlIHNhbXBsZSBub25jZQ==";
    QByteArray request;
    const QString path = m_envUrl.path().isEmpty() ? QStringLiteral("/env") : m_envUrl.path();
    request += "GET " + path.toUtf8() + " HTTP/1.1\r\n";
    request += "Host: " + BackendConfig::hostHeader().toUtf8() + "\r\n";
    request += "Upgrade: websocket\r\n";
    request += "Connection: Upgrade\r\n";
    request += "Sec-WebSocket-Key: " + key + "\r\n";
    request += "Sec-WebSocket-Version: 13\r\n";
    request += "\r\n";
    m_socket->write(request);
}

void EnvMonitorPage::onReadyRead()
{
    m_buffer += m_socket->readAll();
    if (!m_handshakeDone) {
        processHandshake();
    }
    if (m_handshakeDone) {
        processFrames();
    }
}

void EnvMonitorPage::processHandshake()
{
    const int end = m_buffer.indexOf("\r\n\r\n");
    if (end < 0) {
        return;
    }

    const QByteArray header = m_buffer.left(end);
    m_buffer.remove(0, end + 4);
    if (!header.startsWith("HTTP/1.1 101") && !header.startsWith("HTTP/1.0 101")) {
        qWarning() << "EnvMonitorPage /env WebSocket handshake failed" << header.left(120);
        m_socket->disconnectFromHost();
        return;
    }
    m_handshakeDone = true;
    qDebug() << "EnvMonitorPage /env WebSocket connected";
}

void EnvMonitorPage::processFrames()
{
    /* 查找 WebSocket 帧 */
    while (m_buffer.size() >= 2) {
        quint8 byte0 = static_cast<quint8>(m_buffer.at(0));
        quint8 byte1 = static_cast<quint8>(m_buffer.at(1));
        quint8 opcode = byte0 & 0x0F;
        bool masked = (byte1 & 0x80) != 0;
        quint64 payloadLen = byte1 & 0x7F;
        int headerSize = 2;

        if (payloadLen == 126) {
            if (m_buffer.size() < 4)
                return;
            payloadLen = (static_cast<quint8>(m_buffer.at(2)) << 8)
                         | static_cast<quint8>(m_buffer.at(3));
            headerSize = 4;
        } else if (payloadLen == 127) {
            if (m_buffer.size() < 10)
                return;
            payloadLen = 0;
            for (int i = 0; i < 8; ++i) {
                payloadLen = (payloadLen << 8) | static_cast<quint8>(m_buffer.at(2 + i));
            }
            headerSize = 10;
        }

        QByteArray mask;
        if (masked) {
            if (m_buffer.size() < headerSize + 4)
                return;
            mask = m_buffer.mid(headerSize, 4);
            headerSize += 4;
        }

        int totalSize = headerSize + static_cast<int>(payloadLen);
        if (m_buffer.size() < totalSize)
            return;

        QByteArray payload = m_buffer.mid(headerSize, static_cast<int>(payloadLen));
        if (masked) {
            for (int i = 0; i < payload.size(); ++i) {
                payload[i] = static_cast<char>(payload[i] ^ mask.at(i % 4));
            }
        }

        if (opcode == 0x01) { /* text frame */
            processMessage(payload);
        } else if (opcode == 0x02) { /* binary frame - ignore */ }
        else if (opcode == 0x08) { /* close */
            m_socket->close();
            return;
        } else if (opcode == 0x09) { /* ping - send pong */
            QByteArray pong;
            pong.append(static_cast<char>(0x8A));
            pong.append(static_cast<char>(0x00));
            m_socket->write(pong);
        }

        m_buffer.remove(0, totalSize);
    }
}

void EnvMonitorPage::onDisconnected()
{
    m_connected = false;
    m_handshakeDone = false;
    m_buffer.clear();
    updateStatusLine();
    m_reconnectTimer->start(3000);
}

void EnvMonitorPage::onReconnectTimer()
{
    if (!m_connected) {
        m_socket->connectToHost(m_envUrl.host(), m_envUrl.port(BackendConfig::DefaultPort));
    }
}

void EnvMonitorPage::sendRawSerialJsonToBackend(const QByteArray &payload)
{
    const QByteArray trimmed = payload.trimmed();
    if (trimmed.isEmpty()) {
        return;
    }
    if (!m_socket || !m_connected || !m_handshakeDone || m_socket->state() != QAbstractSocket::ConnectedState) {
        qWarning() << "serial_json dropped because /env websocket is not connected";
        return;
    }
    QJsonObject obj;
    obj.insert(QStringLiteral("type"), QStringLiteral("serial_json"));
    obj.insert(QStringLiteral("payload"), QString::fromUtf8(trimmed));
    sendTextFrame(QJsonDocument(obj).toJson(QJsonDocument::Compact));
    qDebug() << "serial_json sent to /env websocket, bytes=" << trimmed.size();
}

void EnvMonitorPage::sendTextFrame(const QByteArray &payload)
{
    if (!m_socket || !m_connected || !m_handshakeDone || m_socket->state() != QAbstractSocket::ConnectedState) {
        return;
    }

    QByteArray frame;
    frame.append(static_cast<char>(0x81));
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

    QByteArray mask;
    mask.resize(4);
    for (int i = 0; i < 4; ++i) {
        mask[i] = static_cast<char>(QRandomGenerator::global()->generate() & 0xFF);
    }
    frame.append(mask);

    QByteArray masked = payload;
    for (int i = 0; i < masked.size(); ++i) {
        masked[i] = static_cast<char>(masked[i] ^ mask.at(i % 4));
    }
    frame.append(masked);
    m_socket->write(frame);
}

void EnvMonitorPage::processMessage(const QByteArray &msg)
{
    QJsonParseError err;
    QJsonDocument doc = QJsonDocument::fromJson(msg, &err);
    if (err.error != QJsonParseError::NoError || !doc.isObject())
        return;

    QJsonObject obj = doc.object();
    const QString type = obj.value(QStringLiteral("type")).toString();

    if (type == QStringLiteral("env_data")) {
        QJsonObject data = obj.value(QStringLiteral("data")).toObject();
        QVariantMap map;
        for (auto it = data.begin(); it != data.end(); ++it) {
            map.insert(it.key(), it.value().toVariant());
        }
        updateEnvData(map);
    } else if (type == QStringLiteral("alarm")) {
        bool active = obj.value(QStringLiteral("active")).toBool();
        QString message = obj.value(QStringLiteral("message")).toString();
        showAlarm(active, message);
    } else if (type == QStringLiteral("command")) {
        QString message = obj.value(QStringLiteral("message")).toString();
        showCommand(message);
    } else if (type == QStringLiteral("stm32_command")) {
        const QString command = obj.value(QStringLiteral("command")).toString();
        writeSerialCommand(command.toUtf8());
    }
}

void EnvMonitorPage::updateEnvData(const QVariantMap &data)
{
    emit environmentDataUpdated(data);

    m_lastUpdate = QTime::currentTime().toString(QStringLiteral("HH:mm:ss"));
    m_deviceStatus = QStringLiteral("online");
    updateStatusLine();

    for (auto it = m_cards.begin(); it != m_cards.end(); ++it) {
        QString text = QStringLiteral("--");
        if (data.contains(it.key())) {
            QVariant v = data.value(it.key());
#if QT_VERSION >= QT_VERSION_CHECK(6, 0, 0)
            const bool isDouble = v.typeId() == QMetaType::Double;
#else
            const bool isDouble = v.type() == QVariant::Double;
#endif
            if (isDouble) {
                text = QString::number(v.toDouble(), 'f', 1);
            } else if (v.canConvert<double>()) {
                bool ok = false;
                const double value = v.toDouble(&ok);
                if (ok) {
                    text = QString::number(value, 'f', 1);
                }
            }
        }
        it.value()->updateValue(text);
    }
}


void EnvMonitorPage::resizeEvent(QResizeEvent *event)
{
    QWidget::resizeEvent(event);
    arrangeCards();
    updateStatusLine();
}

void EnvMonitorPage::arrangeCards()
{
    if (!m_grid || m_cardOrder.isEmpty()) {
        return;
    }
    while (QLayoutItem *item = m_grid->takeAt(0)) {
        if (item->widget()) {
            item->widget()->setParent(m_grid->parentWidget());
        }
        delete item;
    }

    const int w = width();
    const bool compact = w < 900;
    const int columns = compact ? 2 : 3;
    for (int c = 0; c < 4; ++c) {
        m_grid->setColumnStretch(c, 0);
    }
    for (int r = 0; r < 5; ++r) {
        m_grid->setRowStretch(r, 0);
    }
    for (EnvDataCard *card : m_cardOrder) {
        if (card) {
            card->setMinimumSize(compact ? QSize(170, 126) : QSize(210, 146));
        }
    }
    for (int i = 0; i < m_cardOrder.size(); ++i) {
        if (!compact && i == 6) {
            m_grid->addWidget(m_cardOrder.at(i), 2, 1);
        } else {
            m_grid->addWidget(m_cardOrder.at(i), i / columns, i % columns);
        }
    }
    for (int c = 0; c < columns; ++c) {
        m_grid->setColumnStretch(c, 1);
    }
    const int rows = compact ? ((m_cardOrder.size() + columns - 1) / columns) : 3;
    for (int r = 0; r < rows; ++r) {
        m_grid->setRowStretch(r, 1);
    }
}

void EnvMonitorPage::updateStatusLine()
{
    if (!m_footer) {
        return;
    }
    const QString port = m_serialReader ? m_serialReader->portName() : QStringLiteral("--");
    const QString baud = m_serialReader ? QString::number(m_serialReader->baudRate()) : QStringLiteral("--");
    const QString status = m_serialOnline ? QStringLiteral("online") : QStringLiteral("offline");
    const bool compact = width() < 900;
    if (compact) {
        m_footer->setText(QStringLiteral("%1 · %2 · %3").arg(port, status, m_lastUpdate));
    } else {
        m_footer->setText(QStringLiteral("串口：%1 | 波特率：%2 | 设备：%3 | 状态：%4 | 更新时间：%5")
                              .arg(port, baud, m_deviceName, status, m_lastUpdate));
    }
    m_footer->setProperty("state", m_serialOnline ? QStringLiteral("online") : QStringLiteral("offline"));
    m_footer->style()->unpolish(m_footer);
    m_footer->style()->polish(m_footer);
}

void EnvMonitorPage::showAlarm(bool active, const QString &message)
{
    if (active) {
        m_alarmBanner->setText(QStringLiteral("报警：%1").arg(message));
        m_alarmBanner->show();
    } else {
        m_alarmBanner->hide();
    }
}

void EnvMonitorPage::showCommand(const QString &message)
{
    m_alarmBanner->setText(QStringLiteral("指令：%1").arg(message));
    m_alarmBanner->show();
}

void EnvMonitorPage::refresh()
{
    if (m_serialReader && !m_serialReader->isOpen()) {
        m_serialReader->start();
    }
}
