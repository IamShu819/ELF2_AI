// 路线详情页面 - 展示逐步骤步行指引和语音播报功能
#pragma once

#include <QWidget>

class QLabel;
class QPushButton;
class QTimer;
class QVBoxLayout;
class MockDataService;
class NavigationController;
class MapWidget;
class RouteStepItem;

// 路线详情页面，显示步行路线的步骤列表、地图标注和语音播报控制
class RouteDetailPage : public QWidget
{
    Q_OBJECT

public:
    RouteDetailPage(NavigationController *nav, MockDataService *data, QWidget *parent = nullptr);
    // 刷新页面，重新加载路线步骤和地图信息
    void refresh();

private:
    // 构建路线步骤列表
    void buildSteps();
    // 设置当前高亮步骤
    void setCurrentStep(int index);

    NavigationController *m_nav = nullptr;      // 导航控制器
    MockDataService *m_data = nullptr;          // 数据服务
    MapWidget *m_map = nullptr;                 // 地图组件
    QLabel *m_title = nullptr;                  // 路线标题
    QLabel *m_meta = nullptr;                   // 路线距离和时间信息
    QVBoxLayout *m_stepsLayout = nullptr;       // 步骤列表布局
    QList<RouteStepItem *> m_stepItems;         // 路线步骤项列表
    QPushButton *m_voice = nullptr;             // 语音播报按钮
    QTimer *m_timer = nullptr;                  // 语音播报定时器
    int m_currentStep = 1;                      // 当前播报步骤索引
};
