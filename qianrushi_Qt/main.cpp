/*
 * 智能便民导航终端 —— Qt 主入口文件
 *
 * 负责初始化 QApplication、加载全局样式和字体，
 * 以及通过环境变量触发各模块的自检流程。
 * 自检通过后启动主窗口进入正常交互模式。
 */

#include <QApplication>
#include <QCursor>
#include <QDebug>
#include <QStyleFactory>
#include <QTextStream>
#include <QtGlobal>

#include "core/BackendConfig.h"
#include "core/CallClient.h"
#include "core/MockDataService.h"
#include "core/OsrmRoutingService.h"
#include "core/RealNavigationService.h"
#include "core/TerminalLocationService.h"
#include "MainWindow.h"

/* 应用主入口 */
int main(int argc, char *argv[])
{
    QApplication app(argc, argv);

    /* Fusion 风格：跨平台外观统一 */
    QApplication::setStyle(QStyleFactory::create(QStringLiteral("Fusion")));
    /* 全局字体：微软雅黑 UI，10 号字 */
    QFont font(QStringLiteral("Microsoft YaHei UI"));
    font.setPointSize(10);
    app.setFont(font);
    app.setOverrideCursor(QCursor(Qt::ArrowCursor));

    /* ---- 自检：通话 WebSocket 配置 ---- */
    if (qEnvironmentVariableIsSet("SMARTNAV_CALL_SELFTEST")) {
        QTextStream(stdout)
            << QStringLiteral("call_url=%1\n")
                   .arg(CallClient::configuredUrl().toString());
        return 0;
    }

    /* ---- 自检：后端连接配置 ---- */
    if (qEnvironmentVariableIsSet("SMARTNAV_BACKEND_SELFTEST")) {
        QTextStream(stdout)
            << QStringLiteral("backend_host=%1\nbackend_port=%2\nvoice_url=%3\nenv_url=%4\n")
                   .arg(BackendConfig::host())
                   .arg(BackendConfig::port())
                   .arg(BackendConfig::voiceUrl().toString())
                   .arg(BackendConfig::envUrl().toString());
        return 0;
    }

    /* ---- 自检：定位服务 ---- */
    if (qEnvironmentVariableIsSet("SMARTNAV_LOCATION_SELFTEST")) {
        const TerminalLocation location = TerminalLocationService::currentLocation();
        QTextStream(stdout)
            << QStringLiteral("location_selftest source=%1 fallback=%2 lon=%3 lat=%4 accuracy_m=%5\n")
                   .arg(location.source)
                   .arg(location.isFallback ? QStringLiteral("true") : QStringLiteral("false"))
                   .arg(location.geoCoordinate.x(), 0, 'f', 12)
                   .arg(location.geoCoordinate.y(), 0, 'f', 12)
                   .arg(location.accuracyMeters, 0, 'f', 1);
        return 0;
    }

    /* ---- 自检：导航图加载与路网匹配 ---- */
    if (qEnvironmentVariableIsSet("SMARTNAV_GRAPH_SELFTEST")) {
        RealNavigationService navigation;
        const bool ready = navigation.initialize();
        const QPointF fixedTerminal(114.410465, 30.4865333);
        const TerminalLocation currentTerminal = TerminalLocationService::currentLocation();
        QTextStream(stdout)
            << QStringLiteral("graph_selftest ready=%1 fixed_snap_m=%2 current_source=%3 current_fallback=%4 current_snap_m=%5\n")
                   .arg(ready ? QStringLiteral("true") : QStringLiteral("false"))
                   .arg(navigation.nearestRoadDistanceMeters(fixedTerminal), 0, 'f', 1)
                   .arg(currentTerminal.source)
                   .arg(currentTerminal.isFallback ? QStringLiteral("true") : QStringLiteral("false"))
                   .arg(navigation.nearestRoadDistanceMeters(currentTerminal.geoCoordinate), 0, 'f', 1);
        return 0;
    }

    /* ---- 自检：路径规划（基于模拟数据） ---- */
    if (qEnvironmentVariableIsSet("SMARTNAV_ROUTE_SELFTEST")) {
        MockDataService data;
        const POIInfo poi = data.poiById(QStringLiteral("bus-wut-1"));
        const RouteInfo route = data.routeToPOI(poi);
        QTextStream(stdout)
            << QStringLiteral("route_selftest end=%1 distance_m=%2 minutes=%3 points=%4 steps=%5 first_lon=%6 first_lat=%7\n")
                   .arg(route.endName)
                   .arg(route.totalDistanceMeters)
                   .arg(route.totalDurationMinutes)
                   .arg(route.pathPoints.size())
                   .arg(route.steps.size())
                   .arg(route.pathPoints.isEmpty() ? 0.0 : route.pathPoints.first().x(), 0, 'f', 12)
                   .arg(route.pathPoints.isEmpty() ? 0.0 : route.pathPoints.first().y(), 0, 'f', 12);
        return 0;
    }

    /* ---- 自检：OSRM 路径响应解析 ---- */
    if (qEnvironmentVariableIsSet("SMARTNAV_OSRM_PARSE_SELFTEST")) {
        /* 模拟 OSRM 返回的标准 JSON 路由响应 */
        const QByteArray payload = R"({
            "code": "Ok",
            "routes": [{
                "distance": 253.4,
                "duration": 191.2,
                "geometry": {
                    "type": "LineString",
                    "coordinates": [[114.410465,30.4865333],[114.408000,30.487000],[114.4051202,30.4878673]]
                },
                "legs": [{
                    "steps": [{
                        "distance": 120.1,
                        "duration": 80.0,
                        "name": "校园路",
                        "maneuver": {"type": "depart", "modifier": "straight", "location": [114.410465,30.4865333]}
                    }, {
                        "distance": 133.3,
                        "duration": 111.2,
                        "name": "关山大道",
                        "maneuver": {"type": "turn", "modifier": "right", "location": [114.408000,30.487000]}
                    }, {
                        "distance": 0.0,
                        "duration": 0.0,
                        "name": "",
                        "maneuver": {"type": "arrive", "modifier": "straight", "location": [114.4051202,30.4878673]}
                    }]
                }]
            }]
        })";
        bool ok = false;
        const RouteInfo route = OsrmRoutingService::parseRouteResponse(
            payload, QStringLiteral("关山大道职业技术学院公交站"), &ok);
        QTextStream(stdout)
            << QStringLiteral("osrm_parse_selftest ok=%1 distance_m=%2 minutes=%3 points=%4 steps=%5 first_lon=%6 first_lat=%7 second_step=%8\n")
                   .arg(ok ? QStringLiteral("true") : QStringLiteral("false"))
                   .arg(route.totalDistanceMeters)
                   .arg(route.totalDurationMinutes)
                   .arg(route.pathPoints.size())
                   .arg(route.steps.size())
                   .arg(route.pathPoints.isEmpty() ? 0.0 : route.pathPoints.first().x(), 0, 'f', 12)
                   .arg(route.pathPoints.isEmpty() ? 0.0 : route.pathPoints.first().y(), 0, 'f', 12)
                   .arg(route.steps.size() > 1 ? route.steps.at(1).title : QString());
        return 0;
    }

    /* ---- 正常启动：创建主窗口并进入事件循环 ---- */
    MainWindow window;

    const QByteArray fullscreenEnv = qgetenv("SMARTNAV_FULLSCREEN").trimmed();
    bool fullscreen = false;
    if (fullscreenEnv == "1") {
        fullscreen = true;
    } else if (fullscreenEnv == "0") {
        fullscreen = false;
    } else {
#if defined(Q_OS_LINUX) && defined(__aarch64__)
        fullscreen = true;
#else
        fullscreen = false;
#endif
    }

    if (fullscreen) {
        window.showFullScreen();
    } else {
        window.show();
    }
    return app.exec();
}
