/*!
 * @file       NavigationController.h
 * @brief      页面导航控制器，管理页面栈与当前上下文
 */

#pragma once

#include <QObject>

#include "models/POIInfo.h"
#include "models/RouteInfo.h"

/*! 页面类型枚举 */
enum class PageType {
    ChatHome,       /*!< 聊天主页 */
    ChatReply,      /*!< 聊天回复 */
    MapHome,        /*!< 地图主页 */
    RoutePlan,      /*!< 路线规划 */
    RouteDetail,    /*!< 路线详情 */
    EnvMonitor,     /*!< 环境监测 */
    Call            /*!< 通话服务 */
};

/*! 页面导航控制器，负责页面切换与当前兴趣点、路线的状态管理 */
class NavigationController : public QObject
{
    Q_OBJECT

public:
    explicit NavigationController(QObject *parent = nullptr);

    /*! 获取当前页面类型 */
    PageType currentPage() const;
    /*! 获取上一页面类型 */
    PageType previousPage() const;

    /*! 获取当前选中的兴趣点 */
    POIInfo currentPOI() const;
    /*! 是否已设置当前兴趣点 */
    bool hasCurrentPOI() const;
    /*! 设置当前兴趣点并发射信号 */
    void setCurrentPOI(const POIInfo &poi);

    /*! 获取当前路线 */
    RouteInfo currentRoute() const;
    /*! 设置当前路线并发射信号 */
    void setCurrentRoute(const RouteInfo &route);

public slots:
    /*! 跳转到指定页面 */
    void goToPage(PageType page);
    /*! 返回上一页面 */
    void goBack();

signals:
    /*! 页面切换时发射 */
    void pageChanged(PageType page, PageType previousPage);
    /*! 当前兴趣点变化时发射 */
    void currentPOIChanged(const POIInfo &poi);
    /*! 当前路线变化时发射 */
    void currentRouteChanged(const RouteInfo &route);

private:
    PageType m_currentPage = PageType::ChatHome;    /*!< 当前页面 */
    PageType m_previousPage = PageType::ChatHome;   /*!< 上一页面 */
    POIInfo m_currentPOI;                           /*!< 当前兴趣点 */
    bool m_hasCurrentPOI = false;                   /*!< 是否已设置兴趣点 */
    RouteInfo m_currentRoute;                       /*!< 当前路线 */
};
