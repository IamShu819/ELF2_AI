/*!
 * @file       Haversine.h
 * @brief      半正矢公式计算，提供经纬度距离计算工具函数
 */

#pragma once

#include <QPointF>
#include <QtMath>

/*!
 * 使用半正矢公式计算两点间的球面距离
 * @param a 起点坐标（经度, 纬度）
 * @param b 终点坐标（经度, 纬度）
 * @return 两点间距离（米）
 */
inline double haversineMeters(const QPointF &a, const QPointF &b)
{
    constexpr double earthRadiusMeters = 6371000.0;
    const double lat1 = qDegreesToRadians(a.y());
    const double lat2 = qDegreesToRadians(b.y());
    const double dLat = qDegreesToRadians(b.y() - a.y());
    const double dLon = qDegreesToRadians(b.x() - a.x());
    const double h = qSin(dLat / 2.0) * qSin(dLat / 2.0)
        + qCos(lat1) * qCos(lat2) * qSin(dLon / 2.0) * qSin(dLon / 2.0);
    return earthRadiusMeters * 2.0 * qAtan2(qSqrt(h), qSqrt(1.0 - h));
}
