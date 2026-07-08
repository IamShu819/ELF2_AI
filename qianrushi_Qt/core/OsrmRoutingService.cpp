/*!
 * @file       OsrmRoutingService.cpp
 * @brief      OSRM 路由服务实现，包含 HTTP 请求、响应解析与缓存
 */

#include "OsrmRoutingService.h"

#include "core/Haversine.h"

#include <QJsonArray>
#include <QJsonDocument>
#include <QJsonObject>
#include <QNetworkAccessManager>
#include <QNetworkReply>
#include <QNetworkRequest>
#include <QTimer>
#include <QUrl>
#include <QUrlQuery>

#include <QtMath>

namespace {

/*! 从环境变量获取 OSRM 服务基地址，未配置时返回默认地址 */
QString osrmBaseUrlFromEnvironment()
{
    const QString configured = qEnvironmentVariable("SMARTNAV_OSRM_URL").trimmed();
    if (!configured.isEmpty()) {
        return configured;
    }
    return QStringLiteral("http://127.0.0.1:5000");
}

/*! 根据操作类型和修饰符生成步骤标题 */
QString titleForManeuver(const QString &type, const QString &modifier)
{
    if (type == QStringLiteral("depart")) {
        return QStringLiteral("出发点");
    }
    if (type == QStringLiteral("arrive")) {
        return QStringLiteral("到达目的地");
    }
    if (modifier.contains(QStringLiteral("left"))) {
        return QStringLiteral("左转");
    }
    if (modifier.contains(QStringLiteral("right"))) {
        return QStringLiteral("右转");
    }
    if (modifier == QStringLiteral("straight")) {
        return QStringLiteral("继续直行");
    }
    return QStringLiteral("继续前行");
}

/*! 根据操作类型和修饰符生成方向类型 */
QString directionTypeForManeuver(const QString &type, const QString &modifier)
{
    if (type == QStringLiteral("arrive")) {
        return QStringLiteral("arrive");
    }
    if (modifier.contains(QStringLiteral("left"))) {
        return QStringLiteral("left");
    }
    if (modifier.contains(QStringLiteral("right"))) {
        return QStringLiteral("right");
    }
    return QStringLiteral("straight");
}

/*! 根据 OSRM 步骤对象生成中文步行指令 */
QString instructionForStep(const QJsonObject &step, const QString &endName)
{
    const QJsonObject maneuver = step.value(QStringLiteral("maneuver")).toObject();
    const QString type = maneuver.value(QStringLiteral("type")).toString();
    const QString modifier = maneuver.value(QStringLiteral("modifier")).toString();
    const QString title = titleForManeuver(type, modifier);
    const QString roadName = step.value(QStringLiteral("name")).toString().trimmed();
    const int distance = qMax(1, qRound(step.value(QStringLiteral("distance")).toDouble()));

    if (type == QStringLiteral("depart")) {
        return roadName.isEmpty()
            ? QStringLiteral("从本终端位置出发")
            : QStringLiteral("从本终端位置出发，沿%1步行约 %2 米").arg(roadName).arg(distance);
    }
    if (type == QStringLiteral("arrive")) {
        return QStringLiteral("到达%1附近，请按现场标识前往入口").arg(endName);
    }
    if (roadName.isEmpty()) {
        return QStringLiteral("%1，继续步行约 %2 米").arg(title).arg(distance);
    }
    return QStringLiteral("%1，沿%2步行约 %3 米").arg(title, roadName).arg(distance);
}

/*! 从 OSRM 坐标数组中提取点坐标 */
QPointF pointFromCoordinateArray(const QJsonArray &coordinates)
{
    if (coordinates.size() < 2) {
        return {};
    }
    return QPointF(coordinates.at(0).toDouble(), coordinates.at(1).toDouble());
}

/*! 计算路径点列表中指定范围内的累积距离 */
double routeLengthMeters(const QList<QPointF> &points, int first, int last)
{
    double distance = 0.0;
    for (int i = first + 1; i <= last && i < points.size(); ++i) {
        distance += haversineMeters(points.at(i - 1), points.at(i));
    }
    return distance;
}

/*! 移除路径中的小回路，优化路径平滑度 */
QList<QPointF> removeSmallLoops(const QList<QPointF> &points)
{
    QList<QPointF> cleaned = points;
    constexpr double closeMeters = 10.0;
    constexpr double minDetourMeters = 35.0;
    constexpr double maxLoopMeters = 280.0;

    bool changed = true;
    while (changed) {
        changed = false;
        for (int i = 0; i < cleaned.size() - 3 && !changed; ++i) {
            for (int j = i + 3; j < cleaned.size(); ++j) {
                const double direct = haversineMeters(cleaned.at(i), cleaned.at(j));
                const double detour = routeLengthMeters(cleaned, i, j);
                if (direct <= closeMeters && detour >= minDetourMeters && detour <= maxLoopMeters) {
                    cleaned.erase(cleaned.begin() + i + 1, cleaned.begin() + j);
                    changed = true;
                    break;
                }
            }
        }
    }
    return cleaned;
}

/*! 从 OSRM 几何信息中提取路径点列表 */
QList<QPointF> pathFromGeometry(const QJsonObject &geometry)
{
    QList<QPointF> points;
    const QJsonArray coordinates = geometry.value(QStringLiteral("coordinates")).toArray();
    for (const QJsonValue &value : coordinates) {
        const QJsonArray pair = value.toArray();
        if (pair.size() >= 2) {
            points.append(pointFromCoordinateArray(pair));
        }
    }
    return removeSmallLoops(points);
}

/*! 从 OSRM 路段信息中提取转向步骤列表 */
QList<RouteStep> stepsFromLegs(const QJsonArray &legs, const QString &endName, const QList<QPointF> &pathPoints)
{
    QList<RouteStep> result;
    int index = 1;
    for (const QJsonValue &legValue : legs) {
        const QJsonArray steps = legValue.toObject().value(QStringLiteral("steps")).toArray();
        for (const QJsonValue &stepValue : steps) {
            const QJsonObject step = stepValue.toObject();
            const QJsonObject maneuver = step.value(QStringLiteral("maneuver")).toObject();
            const QString type = maneuver.value(QStringLiteral("type")).toString();
            const QString modifier = maneuver.value(QStringLiteral("modifier")).toString();
            const QJsonArray location = maneuver.value(QStringLiteral("location")).toArray();

            RouteStep routeStep;
            routeStep.index = index++;
            routeStep.title = titleForManeuver(type, modifier);
            routeStep.instruction = instructionForStep(step, endName);
            routeStep.directionType = directionTypeForManeuver(type, modifier);
            routeStep.distanceMeters = qMax(0, qRound(step.value(QStringLiteral("distance")).toDouble()));
            routeStep.point = location.size() >= 2 ? pointFromCoordinateArray(location) : QPointF{};
            result.append(routeStep);
        }
    }

    if (result.isEmpty() && pathPoints.size() >= 2) {
        result.append({1, QStringLiteral("出发点"), QStringLiteral("从本终端位置出发"), QStringLiteral("straight"), 0, pathPoints.first()});
        result.append({2, QStringLiteral("到达目的地"), QStringLiteral("到达%1附近，请按现场标识前往入口").arg(endName), QStringLiteral("arrive"), 0, pathPoints.last()});
    }
    return result;
}

}

/*!
 * 构造函数，使用环境变量或默认地址
 * @param parent 父对象
 */
OsrmRoutingService::OsrmRoutingService(QObject *parent)
    : OsrmRoutingService(QString(), parent)
{
}

/*!
 * 构造函数，指定 OSRM 服务基地址
 * @param baseUrl OSRM 服务基地址
 * @param parent 父对象
 */
OsrmRoutingService::OsrmRoutingService(const QString &baseUrl, QObject *parent)
    : QObject(parent)
    , m_baseUrl(baseUrl.trimmed().isEmpty() ? osrmBaseUrlFromEnvironment() : baseUrl.trimmed())
    , m_manager(new QNetworkAccessManager(this))
    , m_timeoutTimer(new QTimer(this))
{
    if (m_baseUrl.endsWith(QLatin1Char('/'))) {
        m_baseUrl.chop(1);
    }
    m_timeoutTimer->setSingleShot(true);
    connect(m_timeoutTimer, &QTimer::timeout, this, [this]() {
        if (m_activeReply) {
            m_activeReply->abort();
        }
    });
}

/*! 服务是否可用 */
bool OsrmRoutingService::isEnabled() const
{
    return !qEnvironmentVariableIsSet("SMARTNAV_OSRM_DISABLED") && !m_baseUrl.isEmpty();
}

/*!
 * 同步查询缓存的步行路线
 * @param startGeo 起点坐标
 * @param endGeo 终点坐标
 * @param endName 终点名称
 * @return 缓存的路线信息，未缓存时返回空路线
 */
RouteInfo OsrmRoutingService::calculateWalkingRoute(const QPointF &startGeo, const QPointF &endGeo, const QString &endName) const
{
    RouteInfo route;
    route.startName = QStringLiteral("本终端位置");
    route.endName = endName;
    route.feature = QStringLiteral("OSRM 路线尚未缓存，使用本地路网兜底");

    if (!isEnabled()) {
        return route;
    }

    return m_cache.value(cacheKey(startGeo, endGeo), route);
}

/*!
 * 异步请求步行路线
 * @param startGeo 起点坐标
 * @param endGeo 终点坐标
 * @param endName 终点名称
 * @return 请求标识符，可用于匹配响应
 */
int OsrmRoutingService::requestWalkingRoute(const QPointF &startGeo, const QPointF &endGeo, const QString &endName)
{
    const int requestId = ++m_nextRequestId;
    RouteInfo empty;
    empty.startName = QStringLiteral("本终端位置");
    empty.endName = endName;
    empty.feature = QStringLiteral("OSRM 服务未返回可用步行路线");

    if (!isEnabled()) {
        QTimer::singleShot(0, this, [this, requestId, empty]() {
            emit walkingRouteReady(requestId, empty, false);
        });
        return requestId;
    }

    const QString key = cacheKey(startGeo, endGeo);
    if (m_cache.contains(key)) {
        const RouteInfo cached = m_cache.value(key);
        QTimer::singleShot(0, this, [this, requestId, cached]() {
            emit walkingRouteReady(requestId, cached, true);
        });
        return requestId;
    }

    cancelActiveRequest();

    QNetworkRequest request(QUrl(routeUrl(startGeo, endGeo)));
    request.setHeader(QNetworkRequest::UserAgentHeader, QStringLiteral("SmartNavTerminal/1.0"));
    m_activeReply = m_manager->get(request);
    m_timeoutTimer->start(timeoutMsFor(startGeo, endGeo));

    connect(m_activeReply, &QNetworkReply::finished, this, [this, requestId, endName, key]() {
        QNetworkReply *reply = m_activeReply;
        if (!reply) {
            return;
        }
        m_activeReply = nullptr;
        m_timeoutTimer->stop();

        RouteInfo route;
        route.startName = QStringLiteral("本终端位置");
        route.endName = endName;

        bool ok = false;
        if (reply->error() == QNetworkReply::NoError) {
            route = parseRouteResponse(reply->readAll(), endName, &ok);
            if (ok) {
                m_cache.insert(key, route);
            }
        } else {
            route.feature = QStringLiteral("OSRM 请求超时或失败，使用本地路网兜底");
        }

        reply->deleteLater();
        emit walkingRouteReady(requestId, route, ok);
    });
    return requestId;
}

/*! 取消当前活跃的请求 */
void OsrmRoutingService::cancelActiveRequest()
{
    if (!m_activeReply) {
        return;
    }
    QNetworkReply *reply = m_activeReply;
    m_activeReply = nullptr;
    m_timeoutTimer->stop();
    reply->disconnect(this);
    reply->abort();
    reply->deleteLater();
}

/*!
 * 解析 OSRM 响应的 JSON 数据，返回路线信息
 * @param payload OSRM 响应数据
 * @param endName 终点名称
 * @param ok 输出参数，解析成功时置为 true
 * @return 解析后的路线信息
 */
RouteInfo OsrmRoutingService::parseRouteResponse(const QByteArray &payload, const QString &endName, bool *ok)
{
    if (ok) {
        *ok = false;
    }

    RouteInfo route;
    route.startName = QStringLiteral("本终端位置");
    route.endName = endName;

    QJsonParseError parseError;
    const QJsonDocument doc = QJsonDocument::fromJson(payload, &parseError);
    if (parseError.error != QJsonParseError::NoError || !doc.isObject()) {
        return route;
    }

    const QJsonObject root = doc.object();
    if (root.value(QStringLiteral("code")).toString() != QStringLiteral("Ok")) {
        return route;
    }

    const QJsonArray routes = root.value(QStringLiteral("routes")).toArray();
    if (routes.isEmpty()) {
        return route;
    }

    const QJsonObject osrmRoute = routes.first().toObject();
    route.totalDistanceMeters = qRound(osrmRoute.value(QStringLiteral("distance")).toDouble());
    route.totalDurationMinutes = qMax(1, qCeil(osrmRoute.value(QStringLiteral("duration")).toDouble() / 60.0));
    route.pathPoints = pathFromGeometry(osrmRoute.value(QStringLiteral("geometry")).toObject());
    route.steps = stepsFromLegs(osrmRoute.value(QStringLiteral("legs")).toArray(), endName, route.pathPoints);
    route.feature = QStringLiteral("基于本地 OSRM 步行路网规划，优先沿 OSM 人行道路通行");

    if (route.pathPoints.size() < 2) {
        return route;
    }
    if (route.steps.isEmpty()) {
        route.steps.append({1, QStringLiteral("出发点"), QStringLiteral("从本终端位置出发"), QStringLiteral("straight"), 0, route.pathPoints.first()});
        route.steps.append({2, QStringLiteral("到达目的地"), QStringLiteral("到达%1附近，请按现场标识前往入口").arg(endName), QStringLiteral("arrive"), 0, route.pathPoints.last()});
    }

    if (ok) {
        *ok = true;
    }
    return route;
}

/*!
 * 构建 OSRM 路由请求 URL（步行模式）
 * @param startGeo 起点坐标
 * @param endGeo 终点坐标
 * @return 完整的请求 URL
 */
QString OsrmRoutingService::routeUrl(const QPointF &startGeo, const QPointF &endGeo) const
{
    QString url = QStringLiteral("%1/route/v1/foot/%2,%3;%4,%5")
                      .arg(m_baseUrl)
                      .arg(startGeo.x(), 0, 'f', 7)
                      .arg(startGeo.y(), 0, 'f', 7)
                      .arg(endGeo.x(), 0, 'f', 7)
                      .arg(endGeo.y(), 0, 'f', 7);

    QUrl parsed(url);
    QUrlQuery query;
    query.addQueryItem(QStringLiteral("steps"), QStringLiteral("true"));
    query.addQueryItem(QStringLiteral("geometries"), QStringLiteral("geojson"));
    query.addQueryItem(QStringLiteral("overview"), QStringLiteral("simplified"));
    parsed.setQuery(query);
    return parsed.toString(QUrl::FullyEncoded);
}

/*!
 * 生成请求缓存键
 * @param startGeo 起点坐标
 * @param endGeo 终点坐标
 * @return 基于坐标精度的缓存键字符串
 */
QString OsrmRoutingService::cacheKey(const QPointF &startGeo, const QPointF &endGeo) const
{
    return QStringLiteral("%1,%2;%3,%4")
        .arg(startGeo.x(), 0, 'f', 6)
        .arg(startGeo.y(), 0, 'f', 6)
        .arg(endGeo.x(), 0, 'f', 6)
        .arg(endGeo.y(), 0, 'f', 6);
}

/*!
 * 根据起点到终点的直线距离动态计算超时时间
 * @param startGeo 起点坐标
 * @param endGeo 终点坐标
 * @return 超时时间（毫秒）
 */
int OsrmRoutingService::timeoutMsFor(const QPointF &startGeo, const QPointF &endGeo) const
{
    const double straightDistance = haversineMeters(startGeo, endGeo);
    const int scaled = m_baseTimeoutMs + qRound(straightDistance * 0.35);
    return qBound(m_baseTimeoutMs, scaled, m_maxTimeoutMs);
}
