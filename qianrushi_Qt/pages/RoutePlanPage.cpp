// 路线规划页面实现 - 展示目的地路线信息、地图标注和交互操作
#include "RoutePlanPage.h"

#include <QFrame>
#include <QHBoxLayout>
#include <QLabel>
#include <QPushButton>
#include <QVBoxLayout>

#include "core/MockDataService.h"
#include "core/NavigationController.h"
#include "widgets/MapWidget.h"

// 构造函数，初始化路线规划页面布局和交互
RoutePlanPage::RoutePlanPage(NavigationController *nav, MockDataService *data, QWidget *parent)
    : QWidget(parent)
    , m_nav(nav)
    , m_data(data)
{
    auto *root = new QHBoxLayout(this);
    root->setContentsMargins(28, 16, 28, 16);
    root->setSpacing(20);

    auto *info = new QFrame(this);
    info->setObjectName(QStringLiteral("PageCard"));
    info->setMinimumWidth(360);
    auto *infoLayout = new QVBoxLayout(info);
    infoLayout->setContentsMargins(24, 22, 24, 22);
    infoLayout->setSpacing(16);

    auto *back = new QPushButton(QStringLiteral("← 返回"), info);
    back->setObjectName(QStringLiteral("SecondaryButton"));
    back->setCursor(Qt::PointingHandCursor);
    m_title = new QLabel(info);
    m_title->setObjectName(QStringLiteral("SectionTitle"));
    m_title->setWordWrap(true);

    m_meta = new QLabel(info);
    m_meta->setObjectName(QStringLiteral("BodyTextLarge"));
    m_meta->setWordWrap(true);
    m_feature = new QLabel(info);
    m_feature->setObjectName(QStringLiteral("MetaText"));
    m_feature->setWordWrap(true);
    auto *detail = new QPushButton(QStringLiteral("查看路线"), info);
    detail->setObjectName(QStringLiteral("PrimaryButton"));
    detail->setCursor(Qt::PointingHandCursor);
    auto *reselect = new QPushButton(QStringLiteral("重新选择目的地"), info);
    reselect->setObjectName(QStringLiteral("SecondaryButton"));
    reselect->setCursor(Qt::PointingHandCursor);

    infoLayout->addWidget(back, 0, Qt::AlignLeft);
    infoLayout->addWidget(m_title);
    infoLayout->addWidget(m_meta);
    infoLayout->addWidget(m_feature);
    infoLayout->addWidget(detail);
    infoLayout->addWidget(reselect);
    infoLayout->addStretch(1);

    auto *mapCard = new QFrame(this);
    mapCard->setObjectName(QStringLiteral("PageCard"));
    auto *mapLayout = new QVBoxLayout(mapCard);
    mapLayout->setContentsMargins(18, 18, 18, 18);
    m_map = new MapWidget(mapCard);
    m_map->setPOIs(m_data->allPOIs());
    mapLayout->addWidget(m_map);

    root->addWidget(info, 35);
    root->addWidget(mapCard, 65);

    connect(back, &QPushButton::clicked, m_nav, [this]() { m_nav->goBack(); });
    connect(detail, &QPushButton::clicked, m_nav, [this]() { m_nav->goToPage(PageType::RouteDetail); });
    connect(reselect, &QPushButton::clicked, m_nav, [this]() { m_nav->goToPage(PageType::MapHome); });
    connect(m_data, &MockDataService::routeToPOIReady, this,
            [this](int requestId, const POIInfo &poi, const RouteInfo &route) {
        if (requestId != m_pendingRouteRequestId) {
            return;
        }
        m_pendingRouteRequestId = 0;
        m_nav->setCurrentPOI(poi);
        m_nav->setCurrentRoute(route);
        applyRoute(poi, route);
    });
}

// 刷新页面，重新加载当前POI的路线信息
void RoutePlanPage::refresh()
{
    POIInfo poi = m_nav->hasCurrentPOI() ? m_nav->currentPOI() : m_data->poiById(QStringLiteral("park-lingjiashan"));
    RouteInfo route = m_nav->currentRoute();
    if (route.endName.isEmpty() || route.endName != poi.name) {
        route = m_data->routeToPOI(poi);
        m_nav->setCurrentRoute(route);
    }

    applyRoute(poi, route);
    m_feature->setText(QStringLiteral("路线特点：正在刷新真实步行路线..."));
    m_pendingRouteRequestId = m_data->requestRouteToPOI(poi);
}

// 应用路线数据，更新目的地标题、距离信息和地图路线标注
void RoutePlanPage::applyRoute(const POIInfo &poi, const RouteInfo &route)
{
    m_title->setText(QStringLiteral("去 %1").arg(poi.name));
    m_meta->setText(QStringLiteral("距离：约 %1 米\n预计时间：约 %2 分钟").arg(route.totalDistanceMeters).arg(route.totalDurationMinutes));
    m_feature->setText(QStringLiteral("路线特点：%1").arg(route.feature));
    m_map->setPOIs(m_data->allPOIs());
    m_map->setSelectedPOI(poi.id);
    m_map->setRoute(route);
}
