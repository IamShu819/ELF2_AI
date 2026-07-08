/*!
 * @file       MockDataService.h
 * @brief      模拟数据服务，提供兴趣点查询与路线规划功能
 */

#pragma once

#include <QHash>
#include <QObject>

#include "models/POIInfo.h"
#include "models/RouteInfo.h"
#include "core/OsrmRoutingService.h"
#include "core/RealNavigationService.h"

/*! 模拟数据服务，管理内置兴趣点数据，提供同步/异步路线查询和缓存 */
class MockDataService : public QObject
{
    Q_OBJECT

public:
    explicit MockDataService(QObject *parent = nullptr);

    /*! 获取所有兴趣点列表 */
    QList<POIInfo> allPOIs() const;
    /*! 按类型筛选兴趣点 */
    QList<POIInfo> poisByType(POIType type) const;
    /*! 按标识符查找兴趣点 */
    POIInfo poiById(const QString &id) const;
    /*! 按关键词匹配兴趣点 */
    POIInfo poiByKeyword(const QString &text) const;
    /*! 同步获取到指定兴趣点的路线（优先使用缓存） */
    RouteInfo routeToPOI(const POIInfo &poi) const;
    /*! 获取备选路线 */
    RouteInfo alternateRouteToPOI(const POIInfo &poi) const;
    /*! 异步请求路线规划，返回请求标识符 */
    int requestRouteToPOI(const POIInfo &poi);
    /*! 取消当前挂起的路线请求 */
    void cancelRouteRequest();

signals:
    /*! 异步路线规划完成时发射 */
    void routeToPOIReady(int requestId, const POIInfo &poi, const RouteInfo &route);

private:
    /*! 构建兜底路线（当 OSRM 不可用时） */
    RouteInfo buildFallbackRouteToPOI(const POIInfo &poi) const;
    /*! 生成路线缓存键 */
    QString routeCacheKey(const POIInfo &poi) const;

    QList<POIInfo> m_pois;                                          /*!< 内置兴趣点列表 */
    mutable OsrmRoutingService m_osrmRoutingService;                /*!< OSRM 在线路由服务 */
    mutable RealNavigationService m_navigationService;              /*!< 本地离线导航服务 */
    mutable bool m_navigationReady = false;                         /*!< 导航服务是否就绪 */
    int m_nextRouteRequestId = 0;                                   /*!< 下一个请求标识号 */
    QHash<int, int> m_pendingRouteRequests;                         /*!< 挂起的请求映射（OSRM请求标识 -> 应用请求标识） */
    QHash<int, POIInfo> m_pendingRoutePOIs;                         /*!< 挂起请求对应的兴趣点 */
    mutable QHash<QString, RouteInfo> m_routeCache;                 /*!< 路线缓存 */
    mutable QPointF m_cachedStartGeo;                               /*!< 缓存的起点位置，避免重复调用定位服务 */
    mutable bool m_cachedStartGeoValid = false;                     /*!< 缓存起点是否有效 */
};
