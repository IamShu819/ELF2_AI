/*
 * 文件：MapWidget.h
 * 说明：地图显示控件，支持瓦片底图渲染、POI标注、路径绘制及交互操作
 */

#pragma once

#include <QWidget>

#include "core/TileMapProvider.h"
#include "models/POIInfo.h"
#include "models/RouteInfo.h"

class QVariantAnimation;

/*
 * 地图显示控件
 * 提供瓦片底图、兴趣点（POI）标注、导航路径绘制、拖拽平移及点击交互功能
 */
class MapWidget : public QWidget
{
    Q_OBJECT
    /* 当前定位点脉冲动画属性 */
    Q_PROPERTY(qreal locationPulse READ locationPulse WRITE setLocationPulse)
    /* 路径绘制进度动画属性 */
    Q_PROPERTY(qreal routeProgress READ routeProgress WRITE setRouteProgress)

public:
    explicit MapWidget(QWidget *parent = nullptr);

    /* 设置兴趣点列表 */
    void setPOIs(const QList<POIInfo> &pois);
    /* 选中指定 ID 的兴趣点 */
    void setSelectedPOI(const QString &poiId);
    /* 设置导航路径并启动绘制动画 */
    void setRoute(const RouteInfo &route);
    /* 清除当前导航路径 */
    void clearRoute();
    /* 设置当前导航步骤索引 */
    void setCurrentStep(int stepIndex);
    /* 仅显示指定类型的兴趣点 */
    void setShowOnlyType(POIType type);
    /* 清除类型筛选，显示全部兴趣点 */
    void clearTypeFilter();
    /* 启动路径绘制动画 */
    void startRouteAnimation();

    /* 获取当前定位脉冲值 */
    qreal locationPulse() const;
    /* 设置定位脉冲值 */
    void setLocationPulse(qreal value);
    /* 获取路径绘制进度值 */
    qreal routeProgress() const;
    /* 设置路径绘制进度值 */
    void setRouteProgress(qreal value);

signals:
    /* 兴趣点被点击时发射 */
    void poiClicked(const POIInfo &poi);

protected:
    void paintEvent(QPaintEvent *event) override;
    void mousePressEvent(QMouseEvent *event) override;
    void mouseReleaseEvent(QMouseEvent *event) override;
    void mouseMoveEvent(QMouseEvent *event) override;
    void leaveEvent(QEvent *event) override;

private:
    /* 获取地图绘制区域（内边距缩进后的矩形） */
    QRectF mapRect() const;
    /* 将地图坐标转换为控件坐标 */
    QPointF toWidgetPoint(const QPointF &mapPoint) const;
    /* 绘制瓦片底图 */
    void drawTileBase(QPainter &painter, const QRectF &rect);
    /* 获取当前可见的兴趣点列表（考虑类型筛选） */
    QList<POIInfo> visiblePOIs() const;
    /* 获取指定坐标处的兴趣点 */
    POIInfo poiAt(const QPoint &pos) const;
    /* 根据兴趣点类型返回对应颜色 */
    QColor colorForType(POIType type) const;
    /* 根据兴趣点类型返回对应图标文字 */
    QString iconForType(POIType type) const;
    /* 绘制导航路径及步骤节点 */
    void drawRoute(QPainter &painter);

    QList<POIInfo> m_pois;                   /* 兴趣点列表 */
    QString m_selectedPoiId;                 /* 当前选中的兴趣点 ID */
    RouteInfo m_route;                       /* 导航路径信息 */
    bool m_hasRoute = false;                 /* 是否显示路径 */
    int m_currentStep = 0;                   /* 当前导航步骤索引 */
    bool m_hasTypeFilter = false;            /* 是否启用类型筛选 */
    POIType m_typeFilter = POIType::Other;   /* 筛选的兴趣点类型 */
    qreal m_locationPulse = 0.0;             /* 定位点脉冲动画值 */
    qreal m_routeProgress = 1.0;             /* 路径绘制进度（0.0 ~ 1.0） */
    QVariantAnimation *m_locationAnimation = nullptr;  /* 定位点脉冲动画 */
    QVariantAnimation *m_routeAnimation = nullptr;     /* 路径绘制动画 */
    TileMapProvider m_tileProvider;          /* 瓦片地图数据提供器 */
    QPointF m_terminalGeoCoordinate = QPointF(114.410465, 30.4865333); /* 缓存定位坐标，避免绘制时阻塞 */
    QString m_terminalName = QStringLiteral("本终端位置");             /* 缓存定位名称 */
    bool m_terminalIsFallback = true;         /* 是否为默认定位 */
    bool m_dragging = false;                 /* 是否正在拖拽地图 */
    QPoint m_lastDragPos;                    /* 上次拖拽鼠标位置 */
    QPoint m_pressPos;                       /* 鼠标按下位置（用于判断拖拽） */
};
