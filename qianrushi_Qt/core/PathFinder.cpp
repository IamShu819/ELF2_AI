/*!
 * @file       PathFinder.cpp
 * @brief      路径查找器实现，基于 A* 算法
 */

#include "PathFinder.h"

#include <QHash>
#include <QSet>
#include <limits>
#include <queue>

#include "core/Haversine.h"
#include "core/RoadGraph.h"

/*! 优先队列节点 */
struct QueueNode {
    int nodeId = -1;        /*!< 节点标识符 */
    double fCost = 0.0;     /*!< 预估总代价（g + h） */
};

/*! 优先队列比较器，按 fCost 升序排列 */
struct QueueCompare {
    bool operator()(const QueueNode &a, const QueueNode &b) const
    {
        return a.fCost > b.fCost;
    }
};

/*!
 * 在路网中查找从起点到终点的最短路径（A* 算法）
 * @param graph 路网图
 * @param startId 起点节点标识符
 * @param endId 终点节点标识符
 * @return 路径节点标识符列表，未找到路径时返回空列表
 */
QList<int> PathFinder::findPath(const RoadGraph &graph, int startId, int endId) const
{
    if (startId < 0 || endId < 0 || !graph.node(startId) || !graph.node(endId)) {
        return {};
    }

    std::priority_queue<QueueNode, std::vector<QueueNode>, QueueCompare> openSet;
    QSet<int> closedSet;
    QHash<int, double> gCost;
    QHash<int, int> parent;

    gCost.insert(startId, 0.0);
    parent.insert(startId, -1);
    openSet.push({startId, heuristic(graph, startId, endId)});

    while (!openSet.empty()) {
        const QueueNode current = openSet.top();
        openSet.pop();

        if (closedSet.contains(current.nodeId)) {
            continue;
        }
        if (current.nodeId == endId) {
            QList<int> path;
            int id = endId;
            while (id != -1) {
                path.prepend(id);
                id = parent.value(id, -1);
            }
            return path;
        }

        closedSet.insert(current.nodeId);
        const RoadNode *currentNode = graph.node(current.nodeId);
        if (!currentNode) {
            continue;
        }

        for (int neighborId : currentNode->neighbors) {
            if (closedSet.contains(neighborId)) {
                continue;
            }
            const double tentative = gCost.value(current.nodeId, std::numeric_limits<double>::infinity())
                + graph.weight(current.nodeId, neighborId);
            if (tentative < gCost.value(neighborId, std::numeric_limits<double>::infinity())) {
                gCost.insert(neighborId, tentative);
                parent.insert(neighborId, current.nodeId);
                openSet.push({neighborId, tentative + heuristic(graph, neighborId, endId)});
            }
        }
    }
    return {};
}

/*!
 * 估算两点间的启发式距离
 * @param graph 路网图
 * @param nodeIdA 节点 A 标识符
 * @param nodeIdB 节点 B 标识符
 * @return 两点间的直线距离（米）
 */
double PathFinder::heuristic(const RoadGraph &graph, int nodeIdA, int nodeIdB) const
{
    const RoadNode *a = graph.node(nodeIdA);
    const RoadNode *b = graph.node(nodeIdB);
    if (!a || !b) {
        return 0.0;
    }
    return haversineMeters(QPointF(a->lon, a->lat), QPointF(b->lon, b->lat));
}
