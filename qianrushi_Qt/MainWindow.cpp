/*
 * 智能便民导航终端 —— 主窗口实现
 *
 * 实现 MainWindow 的构造、样式加载、页面切换与刷新逻辑。
 * 所有子页面在此统一注册到 QStackedWidget 中，
 * 并通过 NavigationController 的信号驱动页面流转。
 */

#include "MainWindow.h"

#include <QApplication>
#include <QDebug>
#include <QFile>
#include <QStackedWidget>
#include <QTimer>
#include <QVBoxLayout>

#include "core/MockChatService.h"
#include "core/MockDataService.h"
#include "pages/ChatHomePage.h"
#include "pages/ChatReplyPage.h"
#include "pages/MapHomePage.h"
#include "pages/RouteDetailPage.h"
#include "pages/RoutePlanPage.h"
#include "pages/EnvMonitorPage.h"
#include "pages/CallPage.h"
#include "core/VoiceClient.h"
#include "core/CallClient.h"
#include "widgets/HeaderBar.h"

/* 构造函数：初始化服务、布局与信号绑定 */
MainWindow::MainWindow(QWidget *parent)
    : QMainWindow(parent)
    , m_nav(new NavigationController(this))
    , m_data(new MockDataService(this))
    , m_chat(new MockChatService(m_data, this))
    , m_voiceClient(new VoiceClient(this))
    , m_callClient(new CallClient(this))
{
    setWindowTitle(QStringLiteral("智能便民导航终端"));
    setMinimumSize(800, 480);
    resize(1024, 600);
    loadStyleSheet();

    /* 创建中央容器与垂直布局 */
    auto *central = new QWidget(this);
    central->setObjectName(QStringLiteral("AppRoot"));
    auto *layout = new QVBoxLayout(central);
    layout->setContentsMargins(0, 0, 0, 0);
    layout->setSpacing(0);

    /* 创建顶栏与页面栈 */
    auto *header = new HeaderBar(central);
    m_stack = new QStackedWidget(central);

    /* 创建所有子页面实例 */
    m_chatHome    = new ChatHomePage(m_nav, m_data, m_chat, m_voiceClient, m_stack);
    m_chatReply   = new ChatReplyPage(m_nav, m_data, m_chat, m_voiceClient, m_stack);
    m_mapHome     = new MapHomePage(m_nav, m_data, m_stack);
    m_routePlan   = new RoutePlanPage(m_nav, m_data, m_stack);
    m_routeDetail = new RouteDetailPage(m_nav, m_data, m_stack);
    m_envMonitor  = new EnvMonitorPage(m_nav, m_stack);
    m_callPage    = new CallPage(m_callClient, m_stack);

    /* 注册页面到栈，记录类型 → 索引映射 */
    m_pageIndex[PageType::ChatHome]    = m_stack->addWidget(m_chatHome);
    m_pageIndex[PageType::ChatReply]   = m_stack->addWidget(m_chatReply);
    m_pageIndex[PageType::MapHome]     = m_stack->addWidget(m_mapHome);
    m_pageIndex[PageType::RoutePlan]   = m_stack->addWidget(m_routePlan);
    m_pageIndex[PageType::RouteDetail] = m_stack->addWidget(m_routeDetail);
    m_pageIndex[PageType::EnvMonitor]  = m_stack->addWidget(m_envMonitor);
    m_pageIndex[PageType::Call]        = m_stack->addWidget(m_callPage);

    /* 组装布局：顶栏在上，页面栈填充剩余空间 */
    layout->addWidget(header);
    layout->addWidget(m_stack, 1);
    setCentralWidget(central);

    /* ---- 信号-槽连接 ---- */

    /* 顶栏按钮 → 导航请求 */
    connect(header, &HeaderBar::chatHomeRequested,
            m_nav,  [this]() { m_nav->goToPage(PageType::ChatHome); });
    connect(header, &HeaderBar::mapRequested,
            m_nav,  [this]() { m_nav->goToPage(PageType::MapHome); });
    connect(header, &HeaderBar::envMonitorRequested,
            m_nav,  [this]() { m_nav->goToPage(PageType::EnvMonitor); });
    connect(header, &HeaderBar::callPageRequested,
            m_nav,  [this]() { m_nav->goToPage(PageType::Call); });

    connect(m_callClient, &CallClient::stateChanged,
            header, [header](CallClient::State state) {
        header->setCallInProgress(state != CallClient::State::Disconnected);
    });
    connect(m_callClient, &CallClient::stateChanged,
            m_voiceClient, [this](CallClient::State state) {
        if (state == CallClient::State::Disconnected) {
            m_voiceClient->resumeAudioAfterCall();
        } else {
            m_voiceClient->suspendAudioForCall();
        }
    });
    connect(m_callClient, &CallClient::errorText, this, [](const QString &message) {
        qWarning().noquote() << "CallClient error:" << message;
    });

    /* 导航控制器 → 切换页面 */
    connect(m_nav, &NavigationController::pageChanged,
            this,  [this](PageType page, PageType) {
        switchToPage(page);
    });

    connect(m_voiceClient, &VoiceClient::streamingStarted, this, [this]() {
        if (m_stack->currentWidget() != m_chatReply) {
            switchToPage(PageType::ChatReply, false);
        }
    });

    connect(m_envMonitor, &EnvMonitorPage::environmentDataUpdated,
            m_voiceClient, &VoiceClient::sendEnvSnapshot);
    connect(m_voiceClient, &VoiceClient::stm32CommandReceived,
            m_envMonitor, &EnvMonitorPage::writeSerialCommand);

    connect(m_nav, &NavigationController::pageChanged,
            this, [this](PageType page, PageType previousPage) {
        if (previousPage == PageType::ChatReply && page == PageType::ChatHome) {
            m_voiceClient->reset();
        }
    });

    /* 导航控制器 → 更新顶栏高亮状态 */
    connect(m_nav, &NavigationController::pageChanged,
            header, [header](PageType page, PageType) {
        if (page == PageType::MapHome) {
            header->setActivePage(QStringLiteral("map"));
        } else if (page == PageType::EnvMonitor) {
            header->setActivePage(QStringLiteral("env"));
        } else if (page == PageType::Call) {
            header->setActivePage(QStringLiteral("call"));
        } else {
            header->setActivePage(QStringLiteral("chat"));
        }
    });

    /* 初始状态：聊天首页 */
    header->setActivePage(QStringLiteral("chat"));
    switchToPage(PageType::ChatHome);
    QTimer::singleShot(0, this, [this]() {
        m_voiceClient->connectToServer();
    });
}

/* 从资源文件加载全局 QSS 样式表 */
void MainWindow::loadStyleSheet()
{
    QFile file(QStringLiteral(":/styles/style.qss"));
    if (file.open(QIODevice::ReadOnly | QIODevice::Text)) {
        qApp->setStyleSheet(QString::fromUtf8(file.readAll()));
    }
}

/* 切换到目标页面：先刷新，再激活 */
void MainWindow::switchToPage(PageType page, bool refresh)
{
    if (!m_pageIndex.contains(page)) {
        return;
    }
    if (refresh) {
        refreshPage(page);
    }
    QWidget *target = m_stack->widget(m_pageIndex.value(page));
    m_stack->setCurrentWidget(target);
}

/* 刷新页面内容（各页面按需实现） */
void MainWindow::refreshPage(PageType page)
{
    switch (page) {
    case PageType::ChatReply:
        m_chatReply->refresh();
        break;
    case PageType::MapHome:
        m_mapHome->refresh();
        break;
    case PageType::RoutePlan:
        m_routePlan->refresh();
        break;
    case PageType::RouteDetail:
        m_routeDetail->refresh();
        break;
    case PageType::ChatHome:
        /* 聊天首页无刷新逻辑 */
        break;
    case PageType::EnvMonitor:
        m_envMonitor->refresh();
        break;
    case PageType::Call:
        break;
    }
}
