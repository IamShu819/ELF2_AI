// 路线详情页面实现 - 包含逐步骤导航、语音播报和路线切换功能
#include "RouteDetailPage.h"

#include <QFrame>
#include <QHBoxLayout>
#include <QLabel>
#include <QPushButton>
#include <QScrollArea>
#include <QTimer>
#include <QVBoxLayout>

#include "core/MockDataService.h"
#include "core/NavigationController.h"
#include "widgets/MapWidget.h"
#include "widgets/RouteStepItem.h"

// 构造函数，初始化路线详情页面布局和交互
RouteDetailPage::RouteDetailPage(NavigationController *nav, MockDataService *data, QWidget *parent)
    : QWidget(parent)
    , m_nav(nav)
    , m_data(data)
{
    auto *root = new QHBoxLayout(this);
    root->setContentsMargins(28, 16, 28, 16);
    root->setSpacing(20);

    auto *stepsCard = new QFrame(this);
    stepsCard->setObjectName(QStringLiteral("PageCard"));
    stepsCard->setMinimumWidth(400);
    auto *left = new QVBoxLayout(stepsCard);
    left->setContentsMargins(22, 20, 22, 20);
    left->setSpacing(12);

    m_title = new QLabel(stepsCard);
    m_title->setObjectName(QStringLiteral("SectionTitle"));
    m_meta = new QLabel(stepsCard);
    m_meta->setObjectName(QStringLiteral("MetaText"));
    left->addWidget(m_title);
    left->addWidget(m_meta);

    auto *scroll = new QScrollArea(stepsCard);
    scroll->setWidgetResizable(true);
    scroll->setFrameShape(QFrame::NoFrame);
    auto *content = new QWidget(scroll);
    m_stepsLayout = new QVBoxLayout(content);
    m_stepsLayout->setContentsMargins(0, 0, 0, 0);
    m_stepsLayout->setSpacing(10);
    scroll->setWidget(content);
    left->addWidget(scroll, 1);

    auto *buttons = new QHBoxLayout;
    m_voice = new QPushButton(QStringLiteral("语音播报"), stepsCard);
    m_voice->setObjectName(QStringLiteral("PrimaryButton"));
    auto *replan = new QPushButton(QStringLiteral("重新规划"), stepsCard);
    replan->setObjectName(QStringLiteral("SecondaryButton"));
    auto *backMap = new QPushButton(QStringLiteral("返回地图"), stepsCard);
    backMap->setObjectName(QStringLiteral("SecondaryButton"));
    for (QPushButton *button : {m_voice, replan, backMap}) {
        button->setCursor(Qt::PointingHandCursor);
        buttons->addWidget(button);
    }
    left->addLayout(buttons);

    auto *mapCard = new QFrame(this);
    mapCard->setObjectName(QStringLiteral("PageCard"));
    auto *mapLayout = new QVBoxLayout(mapCard);
    mapLayout->setContentsMargins(18, 18, 18, 18);
    m_map = new MapWidget(mapCard);
    m_map->setPOIs(m_data->allPOIs());
    mapLayout->addWidget(m_map);

    root->addWidget(stepsCard, 40);
    root->addWidget(mapCard, 60);

    m_timer = new QTimer(this);
    m_timer->setInterval(1200);
    connect(m_timer, &QTimer::timeout, this, [this]() {
        const int next = m_currentStep + 1;
        if (next > m_nav->currentRoute().steps.size()) {
            m_timer->stop();
            m_voice->setText(QStringLiteral("语音播报"));
            setCurrentStep(1);
            return;
        }
        setCurrentStep(next);
    });
    connect(m_voice, &QPushButton::clicked, this, [this]() {
        setCurrentStep(1);
        m_voice->setText(QStringLiteral("正在播报..."));
        m_timer->start();
    });
    connect(replan, &QPushButton::clicked, this, [this]() {
        const POIInfo poi = m_nav->hasCurrentPOI() ? m_nav->currentPOI() : m_data->poiById(QStringLiteral("park-lingjiashan"));
        m_nav->setCurrentRoute(m_data->alternateRouteToPOI(poi));
        refresh();
    });
    connect(backMap, &QPushButton::clicked, m_nav, [this]() { m_nav->goToPage(PageType::MapHome); });
}

// 刷新页面，重新加载路线步骤、地图标注和标题信息
void RouteDetailPage::refresh()
{
    const POIInfo poi = m_nav->hasCurrentPOI() ? m_nav->currentPOI() : m_data->poiById(QStringLiteral("park-lingjiashan"));
    RouteInfo route = m_nav->currentRoute();
    if (route.endName.isEmpty()) {
        route = m_data->routeToPOI(poi);
        m_nav->setCurrentRoute(route);
    }
    m_title->setText(QStringLiteral("去 %1").arg(route.endName));
    m_meta->setText(QStringLiteral("步行 %1 米 · 约 %2 分钟").arg(route.totalDistanceMeters).arg(route.totalDurationMinutes));
    m_map->setPOIs(m_data->allPOIs());
    m_map->setSelectedPOI(poi.id);
    m_map->setRoute(route);
    buildSteps();
    setCurrentStep(1);
}

// 构建路线步骤列表，清空旧列表并重新添加步骤项
void RouteDetailPage::buildSteps()
{
    qDeleteAll(m_stepItems);
    m_stepItems.clear();
    while (QLayoutItem *item = m_stepsLayout->takeAt(0)) {
        delete item->widget();
        delete item;
    }

    for (const RouteStep &step : m_nav->currentRoute().steps) {
        auto *item = new RouteStepItem;
        item->setStep(step);
        m_stepsLayout->addWidget(item);
        m_stepItems.append(item);
        connect(item, &RouteStepItem::clicked, this, &RouteDetailPage::setCurrentStep);
    }
    m_stepsLayout->addStretch(1);
}

// 设置当前高亮步骤，更新步骤项选中状态和地图显示
void RouteDetailPage::setCurrentStep(int index)
{
    m_currentStep = index;
    for (RouteStepItem *item : m_stepItems) {
        item->setSelected(item->stepIndex() == index);
    }
    m_map->setCurrentStep(index);
}
