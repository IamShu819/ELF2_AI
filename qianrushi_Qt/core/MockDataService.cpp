/*!
 * @file       MockDataService.cpp
 * @brief      模拟数据服务实现
 */

#include "MockDataService.h"

#include "core/TerminalLocationService.h"

#include <QTimer>

/*!
 * 构造函数，初始化内置兴趣点数据并连接 OSRM 路由响应
 * @param parent 父对象
 */
MockDataService::MockDataService(QObject *parent)
    : QObject(parent)
    , m_osrmRoutingService(this)
{
    m_pois = {
        {"wut-east", QStringLiteral("武汉职业技术学院东区"), POIType::ServiceCenter, QPointF(0.50, 0.50), QPointF(114.410465, 30.4865333), true, 80, 1, QStringLiteral("终端所在校区，当前以工业中心附近为固定起点"), QStringLiteral(":/assets/service.png")},
        {"bus-wut-1", QStringLiteral("关山大道职业技术学院公交站"), POIType::BusStation, QPointF(0.40, 0.52), QPointF(114.4051202, 30.4878673), true, 540, 8, QStringLiteral("OSM 真实公交站点，位于关山大道沿线"), QStringLiteral(":/assets/bus.png")},
        {"bus-guanggu", QStringLiteral("光谷大道凌家山北路公交站"), POIType::BusStation, QPointF(0.66, 0.50), QPointF(114.4160202, 30.4861283), true, 530, 8, QStringLiteral("OSM 真实公交站点，位于光谷大道附近"), QStringLiteral(":/assets/bus.png")},
        {"hospital-third", QStringLiteral("武汉市第三医院光谷院区"), POIType::Hospital, QPointF(0.40, 0.18), QPointF(114.4054729, 30.5013367), true, 1720, 25, QStringLiteral("OSM/Nominatim 真实医院点，位于关山大道 216 号"), QStringLiteral(":/assets/hospital.png")},
        {"government-guandong", QStringLiteral("关东街道办事处"), POIType::Government, QPointF(0.70, 0.66), QPointF(114.4181925, 30.4807883), true, 1010, 15, QStringLiteral("OSM 真实政务服务点，可办理街道相关事项"), QStringLiteral(":/assets/government.png")},
        {"park-lingjiashan", QStringLiteral("凌家山北路口袋公园"), POIType::Park, QPointF(0.68, 0.42), QPointF(114.4170056, 30.4880647), true, 680, 10, QStringLiteral("OSM 真实绿地/公园区域，适合短途步行到达"), QStringLiteral(":/assets/park.png")},
        {"food-kfc", QStringLiteral("肯德基（关山大道）"), POIType::Other, QPointF(0.39, 0.70), QPointF(114.4049063, 30.4807071), true, 830, 12, QStringLiteral("OSM 真实餐饮点，可作为周边便民服务参考"), QStringLiteral(":/assets/service.png")},
        {"bank-cmb", QStringLiteral("招商银行（关山大道）"), POIType::Other, QPointF(0.40, 0.66), QPointF(114.4050058, 30.4810903), true, 790, 12, QStringLiteral("OSM 真实银行服务点，位于关山大道附近"), QStringLiteral(":/assets/service.png")},
        {"toilet-campus", QStringLiteral("校内公共卫生间"), POIType::Toilet, QPointF(0.52, 0.52), QPointF(114.41085, 30.48605), true, 120, 2, QStringLiteral("校内便民卫生间点位，基于终端服务范围配置"), QStringLiteral(":/assets/toilet.png")}
    };
    connect(&m_osrmRoutingService, &OsrmRoutingService::walkingRouteReady, this,
            [this](int osrmRequestId, const RouteInfo &osrmRoute, bool ok) {
        const int requestId = m_pendingRouteRequests.take(osrmRequestId);
        const POIInfo poi = m_pendingRoutePOIs.take(osrmRequestId);
        if (requestId == 0) {
            return;
        }
        if (poi.id.isEmpty()) {
            return;
        }

        RouteInfo route = ok && osrmRoute.pathPoints.size() >= 2 ? osrmRoute : buildFallbackRouteToPOI(poi);
        m_routeCache.insert(routeCacheKey(poi), route);
        emit routeToPOIReady(requestId, poi, route);
    });
}

/*! 获取所有兴趣点列表 */
QList<POIInfo> MockDataService::allPOIs() const
{
    return m_pois;
}

/*!
 * 按类型筛选兴趣点
 * @param type 兴趣点类型
 * @return 匹配类型的兴趣点列表
 */
QList<POIInfo> MockDataService::poisByType(POIType type) const
{
    QList<POIInfo> result;
    for (const POIInfo &poi : m_pois) {
        if (poi.type == type) {
            result.append(poi);
        }
    }
    return result;
}

/*!
 * 按标识符查找兴趣点
 * @param id 兴趣点标识符
 * @return 匹配的兴趣点，未找到时返回第一个兴趣点
 */
POIInfo MockDataService::poiById(const QString &id) const
{
    for (const POIInfo &poi : m_pois) {
        if (poi.id == id) {
            return poi;
        }
    }
    return m_pois.isEmpty() ? POIInfo{} : m_pois.first();
}

/*!
 * 按关键词匹配兴趣点
 * @param text 查询文本
 * @return 匹配的兴趣点
 */
POIInfo MockDataService::poiByKeyword(const QString &text) const
{
    const QString lower = text.toLower();
    if (lower.contains(QStringLiteral("医院"))) {
        return poiById(QStringLiteral("hospital-third"));
    }
    if (lower.contains(QStringLiteral("公交"))) {
        return poiById(QStringLiteral("bus-wut-1"));
    }
    if (lower.contains(QStringLiteral("卫生间")) || lower.contains(QStringLiteral("厕所"))) {
        return poiById(QStringLiteral("toilet-campus"));
    }
    if (lower.contains(QStringLiteral("政务")) || lower.contains(QStringLiteral("办事"))) {
        return poiById(QStringLiteral("government-guandong"));
    }
    if (lower.contains(QStringLiteral("社区"))) {
        return poiById(QStringLiteral("wut-east"));
    }
    return poiById(QStringLiteral("park-lingjiashan"));
}

/*!
 * 同步获取到指定兴趣点的路线（优先使用缓存）
 * @param poi 目标兴趣点
 * @return 路线信息
 */
RouteInfo MockDataService::routeToPOI(const POIInfo &poi) const
{
    const QString key = routeCacheKey(poi);
    if (m_routeCache.contains(key)) {
        return m_routeCache.value(key);
    }

    return buildFallbackRouteToPOI(poi);
}

/*!
 * 获取备选路线
 * @param poi 目标兴趣点
 * @return 备选路线信息
 */
RouteInfo MockDataService::alternateRouteToPOI(const POIInfo &poi) const
{
    RouteInfo route = buildFallbackRouteToPOI(poi);
    route.feature = QStringLiteral("已基于当前离线路网重新计算；暂无施工/拥堵数据时保持最优步行路线");
    m_routeCache.insert(routeCacheKey(poi), route);
    return route;
}

/*!
 * 异步请求路线规划
 * @param poi 目标兴趣点
 * @return 请求标识符，可用于匹配响应
 */
int MockDataService::requestRouteToPOI(const POIInfo &poi)
{
    const int requestId = ++m_nextRouteRequestId;
    cancelRouteRequest();

    const QString key = routeCacheKey(poi);
    if (m_routeCache.contains(key)) {
        const RouteInfo cached = m_routeCache.value(key);
        QTimer::singleShot(0, this, [this, requestId, poi, cached]() {
            emit routeToPOIReady(requestId, poi, cached);
        });
        return requestId;
    }

    if (!m_cachedStartGeoValid) {
        m_cachedStartGeo = TerminalLocationService::cachedLocation().geoCoordinate;
        m_cachedStartGeoValid = true;
    }

    if (poi.hasGeoCoordinate) {
        const int osrmRequestId = m_osrmRoutingService.requestWalkingRoute(
            m_cachedStartGeo,
            poi.geoCoordinate,
            poi.name);
        m_pendingRouteRequests.insert(osrmRequestId, requestId);
        m_pendingRoutePOIs.insert(osrmRequestId, poi);
        return requestId;
    }

    const RouteInfo fallback = buildFallbackRouteToPOI(poi);
    m_routeCache.insert(key, fallback);
    QTimer::singleShot(0, this, [this, requestId, poi, fallback]() {
        emit routeToPOIReady(requestId, poi, fallback);
    });
    return requestId;
}

/*! 取消当前挂起的路线请求 */
void MockDataService::cancelRouteRequest()
{
    m_pendingRouteRequests.clear();
    m_pendingRoutePOIs.clear();
    m_osrmRoutingService.cancelActiveRequest();
}

/*!
 * 构建兜底路线（当 OSRM 不可用时）
 * @param poi 目标兴趣点
 * @return 基于本地路网或估算的路线
 */
RouteInfo MockDataService::buildFallbackRouteToPOI(const POIInfo &poi) const
{
    if (!m_cachedStartGeoValid) {
        m_cachedStartGeo = TerminalLocationService::cachedLocation().geoCoordinate;
        m_cachedStartGeoValid = true;
    }

    if (!m_navigationReady) {
        m_navigationReady = m_navigationService.initialize();
    }

    if (m_navigationReady && poi.hasGeoCoordinate) {
        RouteInfo realRoute = m_navigationService.calculateRoute(
            m_cachedStartGeo,
            poi.geoCoordinate,
            poi.name);
        if (realRoute.pathPoints.size() >= 2) {
            return realRoute;
        }
    }

    RouteInfo route;
    route.startName = QStringLiteral("本终端位置");
    route.endName = poi.name;
    route.totalDistanceMeters = poi.distanceMeters;
    route.totalDurationMinutes = poi.durationMinutes;
    route.feature = poi.id == QStringLiteral("hospital-third")
        ? QStringLiteral("道路清晰，沿途有醒目标识")
        : QStringLiteral("路线平坦，适合步行");

    const QPointF start = poi.hasGeoCoordinate ? m_cachedStartGeo : QPointF(0.50, 0.50);
    const QPointF end = poi.hasGeoCoordinate ? poi.geoCoordinate : poi.mapPosition;
    const QPointF mid1((start.x() + end.x()) / 2.0, start.y());
    const QPointF mid2(mid1.x(), end.y());
    route.pathPoints = {start, mid1, mid2, end};

    route.steps = {
        {1, QStringLiteral("出发点"), QStringLiteral("从本终端位置出发，沿主通道步行 %1 米").arg(qMax(80, poi.distanceMeters / 4)), QStringLiteral("straight"), qMax(80, poi.distanceMeters / 4), route.pathPoints.value(0)},
        {2, QStringLiteral("转向"), QStringLiteral("在中心路口按蓝色指引转向，继续前行 %1 米").arg(qMax(100, poi.distanceMeters / 3)), QStringLiteral("right"), qMax(100, poi.distanceMeters / 3), route.pathPoints.value(1)},
        {3, QStringLiteral("继续直行"), QStringLiteral("经过社区服务站附近，沿人行道直行 %1 米").arg(qMax(90, poi.distanceMeters / 4)), QStringLiteral("straight"), qMax(90, poi.distanceMeters / 4), route.pathPoints.value(2)},
        {4, QStringLiteral("到达目的地"), QStringLiteral("%1就在前方醒目位置，请注意现场标识").arg(poi.name), QStringLiteral("arrive"), qMax(40, poi.distanceMeters / 8), route.pathPoints.value(3)}
    };
    return route;
}

/*!
 * 生成路线缓存键
 * @param poi 目标兴趣点
 * @return 包含起点和终点坐标的缓存键字符串
 */
QString MockDataService::routeCacheKey(const POIInfo &poi) const
{
    if (!m_cachedStartGeoValid) {
        m_cachedStartGeo = TerminalLocationService::cachedLocation().geoCoordinate;
        m_cachedStartGeoValid = true;
    }
    const QPointF start = m_cachedStartGeo;
    const QPointF end = poi.hasGeoCoordinate ? poi.geoCoordinate : poi.mapPosition;
    return QStringLiteral("%1:%2,%3;%4,%5")
        .arg(poi.id)
        .arg(start.x(), 0, 'f', 6)
        .arg(start.y(), 0, 'f', 6)
        .arg(end.x(), 0, 'f', 6)
        .arg(end.y(), 0, 'f', 6);
}
