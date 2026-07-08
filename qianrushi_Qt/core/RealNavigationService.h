/*!
 * @file       RealNavigationService.h
 * @brief      真实导航服务，基于离线路网的步行路线规划
 */

#pragma once

#include <QPointF>

#include "core/PathFinder.h"
#include "core/RoadGraph.h"
#include "models/RouteInfo.h"

/*! 真实导航服务，基于离线 OSM 路网和 A* 算法进行步行路线规划 */
class RealNavigationService
{
public:
    /*! 初始化导航服务，加载离线路网数据 */
    bool initialize(const QString &roadNetworkPath = QStringLiteral(":/assets/road_network.json"));
    /*! 导航服务是否就绪 */
    bool isReady() const;
    /*! 计算从起点到终点的步行路线 */
    RouteInfo calculateRoute(const QPointF &startGeo, const QPointF &endGeo, const QString &endName) const;
    /*! 计算指定坐标到最近道路节点的距离 */
    double nearestRoadDistanceMeters(const QPointF &geoCoordinate) const;

private:
    /*! 从路径节点列表生成转向步骤 */
    QList<RouteStep> generateSteps(const QList<int> &nodePath, const QString &endName) const;
    /*! 简化路径，剔除冗余节点 */
    QList<int> simplifyPath(const QList<int> &nodePath) const;
    /*! 判断三个连续节点的转向方向 */
    QString turnDirection(const RoadNode *prev, const RoadNode *curr, const RoadNode *next) const;
    /*! 计算三个连续节点构成的转向角度 */
    double turnAngleDegrees(const RoadNode *prev, const RoadNode *curr, const RoadNode *next) const;

    RoadGraph m_roadGraph;              /*!< 离线路网图 */
    PathFinder m_pathFinder;            /*!< 路径查找器 */
    bool m_ready = false;               /*!< 是否已就绪 */
};
