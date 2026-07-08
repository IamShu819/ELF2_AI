#pragma once

#include <QPointF>
#include <QString>

// 兴趣点类型枚举
enum class POIType {
    Park,            // 公园
    Hospital,        // 医院
    BusStation,      // 公交站
    Toilet,          // 公共卫生间
    Government,      // 政务服务
    ServiceCenter,   // 服务中心
    Other            // 其他
};

// 获取兴趣点类型对应的中文名称
inline QString poiTypeName(POIType type)
{
    switch (type) {
    case POIType::Park:
        return QStringLiteral("公园");
    case POIType::Hospital:
        return QStringLiteral("医院");
    case POIType::BusStation:
        return QStringLiteral("公交站");
    case POIType::Toilet:
        return QStringLiteral("公共卫生间");
    case POIType::Government:
        return QStringLiteral("政务服务");
    case POIType::ServiceCenter:
        return QStringLiteral("服务中心");
    case POIType::Other:
        return QStringLiteral("其他");
    }
    return QStringLiteral("其他");
}

// 兴趣点信息结构体
struct POIInfo {
    QString id;              // 兴趣点唯一标识
    QString name;            // 兴趣点名称
    POIType type = POIType::Other;  // 兴趣点类型
    QPointF mapPosition;     // 地图上的像素坐标
    QPointF geoCoordinate;   // 地理经纬度坐标
    bool hasGeoCoordinate = false;  // 是否已获取地理坐标
    int distanceMeters = 0;  // 距离（米）
    int durationMinutes = 0; // 预计耗时（分钟）
    QString description;     // 兴趣点描述信息
    QString imagePath;       // 兴趣点图片路径
};