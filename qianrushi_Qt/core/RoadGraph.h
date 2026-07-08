/*!
 * @file       RoadGraph.h
 * @brief      路网图结构，管理道路节点和边的加权图
 */

#pragma once

#include <QHash>
#include <QList>
#include <QMap>
#include <QPointF>
#include <QString>
#include <QtGlobal>

/*! 道路节点结构 */
struct RoadNode {
    int id = -1;                    /*!< 节点标识符 */
    double lon = 0.0;               /*!< 经度 */
    double lat = 0.0;               /*!< 纬度 */
    double worldX = 0.0;            /*!< 墨卡托投影 X 坐标 */
    double worldY = 0.0;            /*!< 墨卡托投影 Y 坐标 */
    QList<int> neighbors;           /*!< 邻接节点标识符列表 */
    QMap<int, double> weights;      /*!< 邻接边权重映射（目标节点标识 -> 权重） */
};

/*! 路网图，支持从 Overpass JSON 和 GeoJSON 格式加载道路数据 */
class RoadGraph
{
public:
    /*! 从 Overpass JSON 格式文件加载路网 */
    bool loadFromOverpassJson(const QString &path);
    /*! 从 GeoJSON 格式文件加载路网 */
    bool loadFromGeoJSON(const QString &path);
    /*! 获取指定标识符的节点（常量版本） */
    const RoadNode *node(int id) const;
    /*! 获取指定标识符的节点（可变版本） */
    RoadNode *node(int id);
    /*! 查找距离指定经纬度最近的节点标识符 */
    int findNearestNode(double lon, double lat) const;
    /*! 计算到最近道路节点的距离（米） */
    double nearestNodeDistanceMeters(double lon, double lat) const;
    /*! 获取所有节点标识符列表 */
    QList<int> nodeIds() const;
    /*! 获取两个节点间的边权重 */
    double weight(int fromId, int toId) const;
    /*! 路网是否为空 */
    bool isEmpty() const;

private:
    /*! 经纬度转世界像素坐标 */
    static QPointF lonLatToWorldPixel(double lon, double lat, int zoom = 16, int tileSize = 256);
    /*! 获取或创建指定坐标的节点 */
    int getOrCreateNode(double lon, double lat);
    /*! 生成坐标去重键 */
    QString coordinateKey(double lon, double lat) const;
    /*! 生成空间网格键（基于经纬度） */
    qint64 gridKey(double lon, double lat) const;
    /*! 生成空间网格键（基于网格坐标） */
    qint64 gridKey(int gx, int gy) const;
    /*! 根据道路类型获取权重系数 */
    double highwayWeightMultiplier(const QString &highway) const;
    /*! 添加双向边 */
    void addEdge(int fromId, int toId, double weight);

    QMap<int, RoadNode> m_nodes;                    /*!< 节点映射（标识符 -> 节点） */
    QHash<QString, int> m_coordinateIndex;          /*!< 坐标去重索引 */
    QMultiMap<qint64, int> m_spatialIndex;          /*!< 空间网格索引，加速邻近查询 */
    int m_nextNodeId = 1;                           /*!< 下一个可用节点标识符 */
};
