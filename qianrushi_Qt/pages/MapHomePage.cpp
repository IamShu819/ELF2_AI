// 地图首页页面实现 - 只展示终端当前位置
#include "MapHomePage.h"

#include <QFrame>
#include <QLabel>
#include <QVBoxLayout>

#include "core/MockDataService.h"
#include "widgets/MapWidget.h"

MapHomePage::MapHomePage(NavigationController *nav, MockDataService *data, QWidget *parent)
    : QWidget(parent)
    , m_nav(nav)
    , m_data(data)
{
    auto *root = new QVBoxLayout(this);
    root->setContentsMargins(28, 16, 28, 16);
    root->setSpacing(0);

    auto *main = new QFrame(this);
    main->setObjectName(QStringLiteral("PageCard"));
    auto *layout = new QVBoxLayout(main);
    layout->setContentsMargins(24, 22, 24, 22);
    layout->setSpacing(14);

    auto *title = new QLabel(QStringLiteral("设备当前位置"), main);
    title->setObjectName(QStringLiteral("SectionTitle"));
    auto *subtitle = new QLabel(QStringLiteral("终端定位与周边底图"), main);
    subtitle->setObjectName(QStringLiteral("MetaText"));
    subtitle->setWordWrap(true);

    m_map = new MapWidget(main);
    m_map->setPOIs({});
    m_map->clearRoute();
    m_map->setMinimumHeight(520);

    layout->addWidget(title);
    layout->addWidget(subtitle);
    layout->addWidget(m_map, 1);
    root->addWidget(main, 1);
}

void MapHomePage::refresh()
{
    m_map->setPOIs({});
    m_map->clearRoute();
}
