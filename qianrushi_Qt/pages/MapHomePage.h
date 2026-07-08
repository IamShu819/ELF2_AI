// 地图首页 - 展示设备当前位置
#pragma once

#include <QWidget>

class QLabel;
class MockDataService;
class NavigationController;
class MapWidget;

// 地图首页页面，保持地图查询入口轻量，只显示当前位置
class MapHomePage : public QWidget
{
    Q_OBJECT

public:
    MapHomePage(NavigationController *nav, MockDataService *data, QWidget *parent = nullptr);
    void refresh();

private:
    NavigationController *m_nav = nullptr;      // 导航控制器
    MockDataService *m_data = nullptr;          // 数据服务
    MapWidget *m_map = nullptr;                 // 地图组件
};
