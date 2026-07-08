// 路线规划页面 - 展示步行路线详情和地图导航信息
#pragma once

#include <QWidget>

class QLabel;
class MockDataService;
class NavigationController;
class MapWidget;
struct POIInfo;
struct RouteInfo;

// 路线规划页面，显示目的地信息、路线距离和地图路线标注
class RoutePlanPage : public QWidget
{
    Q_OBJECT

public:
    RoutePlanPage(NavigationController *nav, MockDataService *data, QWidget *parent = nullptr);
    // 刷新页面，重新加载当前POI的路线信息
    void refresh();

private:
    // 应用路线数据，更新界面显示和地图标注
    void applyRoute(const POIInfo &poi, const RouteInfo &route);

    NavigationController *m_nav = nullptr;      // 导航控制器
    MockDataService *m_data = nullptr;          // 数据服务
    MapWidget *m_map = nullptr;                 // 地图组件
    QLabel *m_title = nullptr;                  // 目的地标题
    QLabel *m_meta = nullptr;                   // 距离和时间信息
    QLabel *m_feature = nullptr;                // 路线特点描述
    int m_pendingRouteRequestId = 0;            // 待处理的路线请求ID
};
