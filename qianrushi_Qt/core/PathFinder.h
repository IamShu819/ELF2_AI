/*!
 * @file       PathFinder.h
 * @brief      路径查找器，基于 A* 算法在路网中寻路
 */

#pragma once

#include <QList>

class RoadGraph;

/*! 路径查找器，使用 A* 算法在路网中搜索最短路径 */
class PathFinder
{
public:
    /*! 在路网中查找从起点到终点的最短路径 */
    QList<int> findPath(const RoadGraph &graph, int startId, int endId) const;

private:
    /*! 估算两点间的启发式距离（基于半正矢公式） */
    double heuristic(const RoadGraph &graph, int nodeIdA, int nodeIdB) const;
};
