#pragma once

#include <QUrl>
#include <QString>
#include <QtGlobal>

class BackendConfig
{
public:
    static constexpr quint16 DefaultPort = 8080;

    static QString host();
    static quint16 port();
    static QUrl voiceUrl();
    static QUrl envUrl();
    static QString hostHeader();
};
