/*!
 * @file       RoadGraph.cpp
 * @brief      路网图实现，支持多种格式加载与邻近查询
 */

#include "RoadGraph.h"

#include <QFile>
#include <QJsonArray>
#include <QJsonDocument>
#include <QJsonObject>
#include <QtMath>

#include <limits>

#include "core/Haversine.h"

/*!
 * 从 Overpass JSON 格式文件加载路网
 * @param path 文件路径
 * @return 加载成功返回 true
 */
bool RoadGraph::loadFromOverpassJson(const QString &path)
{
    QFile file(path);
    if (!file.open(QIODevice::ReadOnly)) {
        return false;
    }

    const QJsonDocument doc = QJsonDocument::fromJson(file.readAll());
    const QJsonArray elements = doc.object().value(QStringLiteral("elements")).toArray();
    if (elements.isEmpty()) {
        return false;
    }

    m_nodes.clear();
    m_coordinateIndex.clear();
    m_spatialIndex.clear();
    m_nextNodeId = 1;

    for (const QJsonValue &value : elements) {
        const QJsonObject way = value.toObject();
        if (way.value(QStringLiteral("type")).toString() != QStringLiteral("way")) {
            continue;
        }
        const QJsonArray geometry = way.value(QStringLiteral("geometry")).toArray();
        if (geometry.size() < 2) {
            continue;
        }
        const QJsonObject tags = way.value(QStringLiteral("tags")).toObject();
        const QString highway = tags.value(QStringLiteral("highway")).toString();
        const QString oneway = tags.value(QStringLiteral("oneway")).toString().toLower();
        const bool isOneWay = oneway == QStringLiteral("yes") || oneway == QStringLiteral("true") || oneway == QStringLiteral("1");
        const double multiplier = highwayWeightMultiplier(highway);

        int previousId = -1;
        QPointF previousGeo;
        for (const QJsonValue &pointValue : geometry) {
            const QJsonObject point = pointValue.toObject();
            const double lon = point.value(QStringLiteral("lon")).toDouble();
            const double lat = point.value(QStringLiteral("lat")).toDouble();
            if (qFuzzyIsNull(lon) || qFuzzyIsNull(lat)) {
                continue;
            }
            const int currentId = getOrCreateNode(lon, lat);
            const QPointF currentGeo(lon, lat);
            if (previousId > 0 && previousId != currentId) {
                const double distance = haversineMeters(previousGeo, currentGeo);
                if (distance > 0.5) {
                    addEdge(previousId, currentId, distance * multiplier);
                    if (!isOneWay) {
                        addEdge(currentId, previousId, distance * multiplier);
                    }
                }
            }
            previousId = currentId;
            previousGeo = currentGeo;
        }
    }

    return !m_nodes.isEmpty();
}

/*!
 * 从 GeoJSON 格式文件加载路网
 * 若文件不符合 GeoJSON 格式，则回退调用 loadFromOverpassJson
 * @param path 文件路径
 * @return 加载成功返回 true
 */
bool RoadGraph::loadFromGeoJSON(const QString &path)
{
    QFile file(path);
    if (!file.open(QIODevice::ReadOnly)) {
        return false;
    }

    const QJsonDocument doc = QJsonDocument::fromJson(file.readAll());
    const QJsonArray features = doc.object().value(QStringLiteral("features")).toArray();
    if (features.isEmpty()) {
        return loadFromOverpassJson(path);
    }

    m_nodes.clear();
    m_coordinateIndex.clear();
    m_spatialIndex.clear();
    m_nextNodeId = 1;

    for (const QJsonValue &value : features) {
        const QJsonObject feature = value.toObject();
        const QJsonObject geometry = feature.value(QStringLiteral("geometry")).toObject();
        if (geometry.value(QStringLiteral("type")).toString() != QStringLiteral("LineString")) {
            continue;
        }
        const QJsonArray coordinates = geometry.value(QStringLiteral("coordinates")).toArray();
        const QJsonObject properties = feature.value(QStringLiteral("properties")).toObject();
        const double multiplier = highwayWeightMultiplier(properties.value(QStringLiteral("highway")).toString());

        int previousId = -1;
        QPointF previousGeo;
        for (const QJsonValue &coordValue : coordinates) {
            const QJsonArray coord = coordValue.toArray();
            if (coord.size() < 2) {
                continue;
            }
            const QPointF currentGeo(coord.at(0).toDouble(), coord.at(1).toDouble());
            const int currentId = getOrCreateNode(currentGeo.x(), currentGeo.y());
            if (previousId > 0 && previousId != currentId) {
                const double distance = haversineMeters(previousGeo, currentGeo) * multiplier;
                addEdge(previousId, currentId, distance);
                addEdge(currentId, previousId, distance);
            }
            previousId = currentId;
            previousGeo = currentGeo;
        }
    }
    return !m_nodes.isEmpty();
}

/*!
 * 获取指定标识符的节点（常量版本）
 * @param id 节点标识符
 * @return 节点指针，未找到时返回 nullptr
 */
const RoadNode *RoadGraph::node(int id) const
{
    auto it = m_nodes.constFind(id);
    return it == m_nodes.constEnd() ? nullptr : &it.value();
}

/*!
 * 获取指定标识符的节点（可变版本）
 * @param id 节点标识符
 * @return 节点指针，未找到时返回 nullptr
 */
RoadNode *RoadGraph::node(int id)
{
    auto it = m_nodes.find(id);
    return it == m_nodes.end() ? nullptr : &it.value();
}

/*!
 * 查找距离指定经纬度最近的节点标识符
 * 使用空间网格索引加速搜索
 * @param lon 经度
 * @param lat 纬度
 * @return 最近节点标识符，路网为空时返回 -1
 */
int RoadGraph::findNearestNode(double lon, double lat) const
{
    if (m_nodes.isEmpty()) {
        return -1;
    }

    const int centerGx = qFloor(lon * 1000.0);
    const int centerGy = qFloor(lat * 1000.0);
    QList<int> candidates;
    for (int dx = -2; dx <= 2; ++dx) {
        for (int dy = -2; dy <= 2; ++dy) {
            candidates.append(m_spatialIndex.values(gridKey(centerGx + dx, centerGy + dy)));
        }
    }
    if (candidates.isEmpty()) {
        candidates = m_nodes.keys();
    }

    int bestId = -1;
    double bestDistance = std::numeric_limits<double>::max();
    const QPointF target(lon, lat);
    for (int id : candidates) {
        const RoadNode *candidate = node(id);
        if (!candidate) {
            continue;
        }
        const double distance = haversineMeters(target, QPointF(candidate->lon, candidate->lat));
        if (distance < bestDistance) {
            bestDistance = distance;
            bestId = id;
        }
    }
    return bestId;
}

/*!
 * 计算到最近道路节点的距离
 * @param lon 经度
 * @param lat 纬度
 * @return 最近距离（米），路网为空时返回无穷大
 */
double RoadGraph::nearestNodeDistanceMeters(double lon, double lat) const
{
    const int nearestId = findNearestNode(lon, lat);
    const RoadNode *nearest = node(nearestId);
    if (!nearest) {
        return std::numeric_limits<double>::infinity();
    }
    return haversineMeters(QPointF(lon, lat), QPointF(nearest->lon, nearest->lat));
}

/*! 获取所有节点标识符列表 */
QList<int> RoadGraph::nodeIds() const
{
    return m_nodes.keys();
}

/*!
 * 获取两个节点间的边权重
 * @param fromId 起点节点标识符
 * @param toId 终点节点标识符
 * @return 边权重，不存在时返回无穷大
 */
double RoadGraph::weight(int fromId, int toId) const
{
    const RoadNode *from = node(fromId);
    if (!from) {
        return std::numeric_limits<double>::infinity();
    }
    return from->weights.value(toId, std::numeric_limits<double>::infinity());
}

/*! 路网是否为空 */
bool RoadGraph::isEmpty() const
{
    return m_nodes.isEmpty();
}

/*!
 * 经纬度转世界像素坐标（墨卡托投影）
 * @param lon 经度
 * @param lat 纬度
 * @param zoom 缩放级别
 * @param tileSize 瓦片尺寸
 * @return 世界像素坐标
 */
QPointF RoadGraph::lonLatToWorldPixel(double lon, double lat, int zoom, int tileSize)
{
    const double sinLat = qSin(qDegreesToRadians(qBound(-85.05112878, lat, 85.05112878)));
    const double n = qPow(2.0, zoom) * tileSize;
    return QPointF((lon + 180.0) / 360.0 * n,
                   (0.5 - qLn((1.0 + sinLat) / (1.0 - sinLat)) / (4.0 * M_PI)) * n);
}

/*!
 * 获取或创建指定坐标的节点
 * 若坐标已存在则返回现有节点标识符
 * @param lon 经度
 * @param lat 纬度
 * @return 节点标识符
 */
int RoadGraph::getOrCreateNode(double lon, double lat)
{
    const QString key = coordinateKey(lon, lat);
    const auto existing = m_coordinateIndex.constFind(key);
    if (existing != m_coordinateIndex.constEnd()) {
        return existing.value();
    }

    RoadNode node;
    node.id = m_nextNodeId++;
    node.lon = lon;
    node.lat = lat;
    const QPointF world = lonLatToWorldPixel(lon, lat);
    node.worldX = world.x();
    node.worldY = world.y();
    m_nodes.insert(node.id, node);
    m_coordinateIndex.insert(key, node.id);
    m_spatialIndex.insert(gridKey(lon, lat), node.id);
    return node.id;
}

/*!
 * 生成坐标去重键
 * @param lon 经度
 * @param lat 纬度
 * @return 精度为 7 位小数的坐标键字符串
 */
QString RoadGraph::coordinateKey(double lon, double lat) const
{
    return QStringLiteral("%1,%2").arg(lon, 0, 'f', 7).arg(lat, 0, 'f', 7);
}

/*!
 * 生成空间网格键（基于经纬度）
 * @param lon 经度
 * @param lat 纬度
 * @return 网格键
 */
qint64 RoadGraph::gridKey(double lon, double lat) const
{
    const int gx = qFloor(lon * 1000.0);
    const int gy = qFloor(lat * 1000.0);
    return gridKey(gx, gy);
}

/*!
 * 生成空间网格键（基于网格坐标）
 * @param gx 网格 X 坐标
 * @param gy 网格 Y 坐标
 * @return 网格键
 */
qint64 RoadGraph::gridKey(int gx, int gy) const
{
    return (qint64(gx) << 32) ^ quint32(gy);
}

/*!
 * 根据道路类型获取权重系数
 * 人行道等步行友好道路系数较低，主干道系数较高
 * @param highway 道路类型字符串
 * @return 权重系数
 */
double RoadGraph::highwayWeightMultiplier(const QString &highway) const
{
    if (highway == QStringLiteral("footway") || highway == QStringLiteral("pedestrian") || highway == QStringLiteral("path")) {
        return 0.60;
    }
    if (highway == QStringLiteral("steps")) {
        return 0.95;
    }
    if (highway == QStringLiteral("living_street") || highway == QStringLiteral("service") || highway == QStringLiteral("residential")) {
        return 1.0;
    }
    if (highway == QStringLiteral("unclassified") || highway == QStringLiteral("tertiary")) {
        return 1.18;
    }
    if (highway == QStringLiteral("secondary")) {
        return 1.70;
    }
    if (highway == QStringLiteral("primary")) {
        return 2.20;
    }
    return 1.15;
}

/*!
 * 添加双向边
 * @param fromId 起点节点标识符
 * @param toId 终点节点标识符
 * @param weight 边权重
 */
void RoadGraph::addEdge(int fromId, int toId, double weight)
{
    RoadNode *from = node(fromId);
    if (!from) {
        return;
    }
    if (!from->neighbors.contains(toId)) {
        from->neighbors.append(toId);
    }
    const double existing = from->weights.value(toId, std::numeric_limits<double>::infinity());
    if (weight < existing) {
        from->weights.insert(toId, weight);
    }
}
