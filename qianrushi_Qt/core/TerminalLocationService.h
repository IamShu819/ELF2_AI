/*!
 * @file       TerminalLocationService.h
 * @brief      终端定位服务，获取当前设备的地理位置信息
 */

#pragma once

#include <QPointF>
#include <QString>

/*! 终端位置信息结构 */
struct TerminalLocation {
    QString name;               /*!< 位置名称描述 */
    QString source;             /*!< 定位来源（如 winrt、dotnet、fallback） */
    QPointF geoCoordinate;      /*!< 经纬度坐标（经度, 纬度） */
    bool isFallback = true;     /*!< 是否为兜底位置 */
    qreal accuracyMeters = 0.0; /*!< 定位精度（米） */
};

/*! 终端定位服务，提供静态方法获取当前系统定位 */
class TerminalLocationService
{
public:
    /*! 获取当前终端位置，带缓存策略（同一分钟内返回缓存结果） */
    static TerminalLocation currentLocation();
    /*! 立即返回已缓存的位置；尚未完成系统定位时返回默认终端位置 */
    static TerminalLocation cachedLocation();
};
