/*
 * 智能便民导航终端 —— 主窗口
 *
 * 负责整体页面布局管理，包含顶栏 HeaderBar 和底部
 * QStackedWidget 多页面切换。所有子页面的创建、注册、
 * 切换和刷新均由本类统一协调。
 */

#pragma once

#include <QMainWindow>
#include <QMap>

#include "core/NavigationController.h"

class QStackedWidget;
class HeaderBar;
class MockDataService;
class MockChatService;
class ChatHomePage;
class ChatReplyPage;
class MapHomePage;
class RoutePlanPage;
class RouteDetailPage;
class EnvMonitorPage;
class CallPage;
class VoiceClient;
class CallClient;

/* 主窗口：管理全局页面栈与导航 */
class MainWindow : public QMainWindow
{
    Q_OBJECT

public:
    explicit MainWindow(QWidget *parent = nullptr);

private:
    /* 加载全局 QSS 样式表 */
    void loadStyleSheet();
    /* 切换到指定页面（含刷新） */
    void switchToPage(PageType page, bool refresh = true);
    /* 刷新指定页面的内容 */
    void refreshPage(PageType page);

    /* ---- 核心服务 ---- */
    NavigationController *m_nav = nullptr;   /* 页面导航控制器 */
    MockDataService *m_data = nullptr;       /* 模拟数据服务 */
    MockChatService *m_chat = nullptr;       /* 模拟聊天服务 */
    VoiceClient *m_voiceClient = nullptr;    /* 共享语音客户端 */
    CallClient *m_callClient = nullptr;      /* 局域网通话客户端 */

    /* ---- 页面管理 ---- */
    QStackedWidget *m_stack = nullptr;       /* 页面栈容器 */
    QMap<PageType, int> m_pageIndex;         /* 页面类型 → 栈索引映射 */

    /* ---- 子页面实例 ---- */
    ChatHomePage    *m_chatHome    = nullptr;
    ChatReplyPage   *m_chatReply   = nullptr;
    MapHomePage     *m_mapHome     = nullptr;
    RoutePlanPage   *m_routePlan   = nullptr;
    RouteDetailPage *m_routeDetail = nullptr;
    EnvMonitorPage  *m_envMonitor  = nullptr;
    CallPage        *m_callPage    = nullptr;
};
