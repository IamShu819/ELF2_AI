/*!
 * @file       NavigationController.cpp
 * @brief      页面导航控制器实现
 */

#include "NavigationController.h"

/*! 构造函数 */
NavigationController::NavigationController(QObject *parent)
    : QObject(parent)
{
}

/*! 获取当前页面类型 */
PageType NavigationController::currentPage() const
{
    return m_currentPage;
}

/*! 获取上一页面类型 */
PageType NavigationController::previousPage() const
{
    return m_previousPage;
}

/*! 获取当前选中的兴趣点 */
POIInfo NavigationController::currentPOI() const
{
    return m_currentPOI;
}

/*! 是否已设置当前兴趣点 */
bool NavigationController::hasCurrentPOI() const
{
    return m_hasCurrentPOI;
}

/*! 设置当前兴趣点并发射信号 */
void NavigationController::setCurrentPOI(const POIInfo &poi)
{
    m_currentPOI = poi;
    m_hasCurrentPOI = true;
    emit currentPOIChanged(poi);
}

/*! 获取当前路线 */
RouteInfo NavigationController::currentRoute() const
{
    return m_currentRoute;
}

/*! 设置当前路线并发射信号 */
void NavigationController::setCurrentRoute(const RouteInfo &route)
{
    m_currentRoute = route;
    emit currentRouteChanged(route);
}

/*!
 * 跳转到指定页面
 * @param page 目标页面类型
 */
void NavigationController::goToPage(PageType page)
{
    if (page == m_currentPage) {
        emit pageChanged(page, m_previousPage);
        return;
    }
    m_previousPage = m_currentPage;
    m_currentPage = page;
    emit pageChanged(m_currentPage, m_previousPage);
}

/*! 返回上一页面 */
void NavigationController::goBack()
{
    goToPage(m_previousPage);
}
