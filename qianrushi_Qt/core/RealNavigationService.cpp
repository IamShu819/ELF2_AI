/*!
 * @file       RealNavigationService.cpp
 * @brief      真实导航服务实现
 */

#include "RealNavigationService.h"

#include <QtMath>

#include <limits>

#include "core/Haversine.h"

/*!
 * 初始化导航服务，加载离线路网数据
 * @param roadNetworkPath 路网文件路径
 * @return 初始化成功返回 true
 */
bool RealNavigationService::initialize(const QString &roadNetworkPath)
{
    m_ready = m_roadGraph.loadFromOverpassJson(roadNetworkPath);
    return m_ready;
}

/*! 导航服务是否就绪 */
bool RealNavigationService::isReady() const
{
    return m_ready && !m_roadGraph.isEmpty();
}

/*!
 * 计算指定坐标到最近道路节点的距离
 * @param geoCoordinate 经纬度坐标
 * @return 最近道路距离（米），路网未就绪时返回无穷大
 */
double RealNavigationService::nearestRoadDistanceMeters(const QPointF &geoCoordinate) const
{
    if (!isReady()) {
        return std::numeric_limits<double>::infinity();
    }
    return m_roadGraph.nearestNodeDistanceMeters(geoCoordinate.x(), geoCoordinate.y());
}

/*!
 * 计算从起点到终点的步行路线
 * @param startGeo 起点经纬度
 * @param endGeo 终点经纬度
 * @param endName 终点名称
 * @return 包含路径点、距离、时长和转向步骤的路线信息
 */
RouteInfo RealNavigationService::calculateRoute(const QPointF &startGeo, const QPointF &endGeo, const QString &endName) const
{
    RouteInfo route;
    route.startName = QStringLiteral("本终端位置");
    route.endName = endName;
    route.feature = QStringLiteral("基于离线 OSM 路网 A* 规划，优先沿真实道路步行");

    if (!isReady()) {
        return route;
    }

    const int startId = m_roadGraph.findNearestNode(startGeo.x(), startGeo.y());
    const int endId = m_roadGraph.findNearestNode(endGeo.x(), endGeo.y());
    const QList<int> path = m_pathFinder.findPath(m_roadGraph, startId, endId);
    if (path.size() < 2) {
        return route;
    }

    double total = 0.0;
    for (int i = 1; i < path.size(); ++i) {
        const RoadNode *prev = m_roadGraph.node(path[i - 1]);
        const RoadNode *curr = m_roadGraph.node(path[i]);
        if (!prev || !curr) {
            continue;
        }
        total += haversineMeters(QPointF(prev->lon, prev->lat), QPointF(curr->lon, curr->lat));
    }

    const QList<int> simplifiedPath = simplifyPath(path);
    for (int nodeId : simplifiedPath) {
        const RoadNode *node = m_roadGraph.node(nodeId);
        if (!node) {
            continue;
        }
        const QPointF point(node->lon, node->lat);
        route.pathPoints.append(point);
    }

    route.totalDistanceMeters = qRound(total);
    route.totalDurationMinutes = qMax(1, qCeil(total / 80.0));
    route.steps = generateSteps(simplifiedPath, endName);
    if (!route.steps.isEmpty()) {
        route.steps.first().instruction = QStringLiteral("从本终端位置出发，沿最近道路开始步行");
        route.steps.last().instruction = QStringLiteral("到达%1附近，请按现场标识前往入口").arg(endName);
    }
    return route;
}

/*!
 * 从路径节点列表生成转向步骤
 * @param nodePath 路径节点标识符列表
 * @param endName 终点名称
 * @return 转向步骤列表
 */
QList<RouteStep> RealNavigationService::generateSteps(const QList<int> &nodePath, const QString &endName) const
{
    QList<RouteStep> steps;
    if (nodePath.size() < 2) {
        return steps;
    }

    const RoadNode *start = m_roadGraph.node(nodePath.first());
    if (start) {
        steps.append({1, QStringLiteral("出发点"), QStringLiteral("从本终端位置出发"), QStringLiteral("straight"), 0, QPointF(start->lon, start->lat)});
    }

    int stepIndex = 2;
    double segmentDistance = 0.0;
    for (int i = 1; i < nodePath.size() - 1; ++i) {
        const RoadNode *prev = m_roadGraph.node(nodePath[i - 1]);
        const RoadNode *curr = m_roadGraph.node(nodePath[i]);
        const RoadNode *next = m_roadGraph.node(nodePath[i + 1]);
        if (!prev || !curr || !next) {
            continue;
        }
        segmentDistance += haversineMeters(QPointF(prev->lon, prev->lat), QPointF(curr->lon, curr->lat));
        const QString direction = turnDirection(prev, curr, next);
        if (direction != QStringLiteral("straight")) {
            const QString title = direction == QStringLiteral("left")
                ? QStringLiteral("左转")
                : direction == QStringLiteral("right") ? QStringLiteral("右转") : QStringLiteral("继续直行");
            steps.append({
                stepIndex++,
                title,
                QStringLiteral("%1，继续步行约 %2 米").arg(title).arg(qMax(30, qRound(segmentDistance))),
                direction,
                qMax(30, qRound(segmentDistance)),
                QPointF(curr->lon, curr->lat)
            });
            segmentDistance = 0.0;
        }
    }

    const RoadNode *end = m_roadGraph.node(nodePath.last());
    if (end) {
        steps.append({stepIndex, QStringLiteral("到达目的地"), QStringLiteral("到达%1附近").arg(endName), QStringLiteral("arrive"), qMax(20, qRound(segmentDistance)), QPointF(end->lon, end->lat)});
    }
    return steps;
}

/*!
 * 简化路径，剔除冗余节点
 * 仅保留转向角度较大或累积距离较长的关键节点
 * @param nodePath 原始路径节点列表
 * @return 简化后的节点列表
 */
QList<int> RealNavigationService::simplifyPath(const QList<int> &nodePath) const
{
    if (nodePath.size() <= 2) {
        return nodePath;
    }

    QList<int> result;
    result.append(nodePath.first());
    double accumulated = 0.0;
    for (int i = 1; i < nodePath.size() - 1; ++i) {
        const RoadNode *prev = m_roadGraph.node(nodePath[i - 1]);
        const RoadNode *curr = m_roadGraph.node(nodePath[i]);
        const RoadNode *next = m_roadGraph.node(nodePath[i + 1]);
        if (!prev || !curr || !next) {
            continue;
        }
        accumulated += haversineMeters(QPointF(prev->lon, prev->lat), QPointF(curr->lon, curr->lat));
        const double angle = qAbs(turnAngleDegrees(prev, curr, next));
        if (angle >= 18.0 || accumulated >= 420.0) {
            result.append(nodePath[i]);
            accumulated = 0.0;
        }
    }
    result.append(nodePath.last());
    return result;
}

/*!
 * 判断三个连续节点的转向方向
 * @param prev 前一节点
 * @param curr 当前节点
 * @param next 下一节点
 * @return 转向方向（left、right、straight）
 */
QString RealNavigationService::turnDirection(const RoadNode *prev, const RoadNode *curr, const RoadNode *next) const
{
    const double angle = turnAngleDegrees(prev, curr, next);
    if (qAbs(angle) < 30.0) {
        return QStringLiteral("straight");
    }
    return angle > 0.0 ? QStringLiteral("right") : QStringLiteral("left");
}

/*!
 * 计算三个连续节点构成的转向角度
 * @param prev 前一节点
 * @param curr 当前节点
 * @param next 下一节点
 * @return 转向角度（正值为右转，负值为左转）
 */
double RealNavigationService::turnAngleDegrees(const RoadNode *prev, const RoadNode *curr, const RoadNode *next) const
{
    const double ax = curr->worldX - prev->worldX;
    const double ay = curr->worldY - prev->worldY;
    const double bx = next->worldX - curr->worldX;
    const double by = next->worldY - curr->worldY;
    const double cross = ax * by - ay * bx;
    const double dot = ax * bx + ay * by;
    return qRadiansToDegrees(qAtan2(cross, dot));
}
