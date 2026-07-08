#include "BackendConfig.h"

#include <QByteArray>
#include <QUrl>
#include <QtGlobal>

QString BackendConfig::host()
{
    const QString value = QString::fromLocal8Bit(qgetenv("SMARTNAV_BACKEND_HOST")).trimmed();
    if (value.isEmpty()) {
        return QStringLiteral("127.0.0.1");
    }
    return value;
}

quint16 BackendConfig::port()
{
    bool ok = false;
    const QByteArray raw = qgetenv("SMARTNAV_BACKEND_PORT").trimmed();
    const int value = raw.toInt(&ok);
    if (!ok || value <= 0 || value > 65535) {
        return DefaultPort;
    }
    return static_cast<quint16>(value);
}

QUrl BackendConfig::voiceUrl()
{
    QUrl url;
    url.setScheme(QStringLiteral("ws"));
    url.setHost(host());
    url.setPort(port());
    url.setPath(QStringLiteral("/voice"));
    return url;
}

QUrl BackendConfig::envUrl()
{
    QUrl url;
    url.setScheme(QStringLiteral("ws"));
    url.setHost(host());
    url.setPort(port());
    url.setPath(QStringLiteral("/env"));
    return url;
}

QString BackendConfig::hostHeader()
{
    return QStringLiteral("%1:%2").arg(host()).arg(port());
}
