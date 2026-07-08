#pragma once

#include <QList>
#include <QPointF>
#include <QString>

// 路线步骤结构体，表示导航路径中的一个步骤
struct RouteStep {
    int index = 0;              // 步骤序号
    QString title;              // 步骤标题
    QString instruction;        // 步骤文字指引说明
    QString directionType;      // 方向类型（直行、转弯等）
    int distanceMeters = 0;     // 该步骤距离（米）
    QPointF point;              // 该步骤对应的位置坐标
};

// 路线信息结构体，表示从起点到终点的完整导航路线
struct RouteInfo {
    QString startName;              // 起点名称
    QString endName;                // 终点名称
    int totalDistanceMeters = 0;    // 路线总距离（米）
    int totalDurationMinutes = 0;   // 路线总耗时（分钟）
    QString feature;                // 路线特征描述（如"推荐路线"）
    QList<QPointF> pathPoints;      // 路线经过的路径点坐标列表
    QList<RouteStep> steps;         // 路线包含的步骤列表
};