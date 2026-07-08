/*!
 * @file       OsrmRoutingService.h
 * @brief      OSRM 路由服务，通过 HTTP 请求获取 OSRM 步行路线
 */

#pragma once

#include <QByteArray>
#include <QHash>
#include <QObject>
#include <QPointF>
#include <QString>

#include "models/RouteInfo.h"

class QNetworkAccessManager;
class QNetworkReply;
class QTimer;

/*! OSRM 路由服务，向本地 OSRM 后端请求步行路线并缓存结果 */
class OsrmRoutingService : public QObject
{
    Q_OBJECT

public:
    /*! 使用环境变量或默认地址构造 */
    explicit OsrmRoutingService(QObject *parent = nullptr);
    /*! 使用指定基地址构造 */
    explicit OsrmRoutingService(const QString &baseUrl, QObject *parent = nullptr);

    /*! 服务是否可用（未通过环境变量禁用且基地址非空） */
    bool isEnabled() const;
    /*! 同步查询缓存的步行路线 */
    RouteInfo calculateWalkingRoute(const QPointF &startGeo, const QPointF &endGeo, const QString &endName) const;
    /*! 异步请求步行路线，返回请求标识符 */
    int requestWalkingRoute(const QPointF &startGeo, const QPointF &endGeo, const QString &endName);
    /*! 取消当前活跃的请求 */
    void cancelActiveRequest();

    /*! 解析 OSRM 响应的 JSON 数据，返回路线信息 */
    static RouteInfo parseRouteResponse(const QByteArray &payload, const QString &endName, bool *ok = nullptr);

signals:
    /*! 异步路线请求完成时发射 */
    void walkingRouteReady(int requestId, const RouteInfo &route, bool ok);

private:
    /*! 构建 OSRM 路由请求 URL */
    QString routeUrl(const QPointF &startGeo, const QPointF &endGeo) const;
    /*! 生成请求缓存键 */
    QString cacheKey(const QPointF &startGeo, const QPointF &endGeo) const;
    /*! 根据距离动态计算请求超时时间 */
    int timeoutMsFor(const QPointF &startGeo, const QPointF &endGeo) const;

    QString m_baseUrl;                              /*!< OSRM 服务基地址 */
    int m_baseTimeoutMs = 1400;                     /*!< 基础超时时间（毫秒） */
    int m_maxTimeoutMs = 3200;                      /*!< 最大超时时间（毫秒） */
    int m_nextRequestId = 0;                        /*!< 下一个请求标识号 */
    QNetworkAccessManager *m_manager = nullptr;     /*!< 网络访问管理器 */
    QNetworkReply *m_activeReply = nullptr;         /*!< 当前活跃的网络回复 */
    QTimer *m_timeoutTimer = nullptr;               /*!< 请求超时定时器 */
    QHash<QString, RouteInfo> m_cache;              /*!< 路线缓存 */
};
