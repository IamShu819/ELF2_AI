QT += core gui widgets svg network multimedia serialport

TEMPLATE = app
TARGET = qianrushi
CONFIG += c++17

INCLUDEPATH += $$PWD

SOURCES += \
    main.cpp \
    MainWindow.cpp \
    core/BackendConfig.cpp \
    core/CallClient.cpp \
    core/MockChatService.cpp \
    core/MockDataService.cpp \
    core/NavigationController.cpp \
    core/SerialEnvReader.cpp \
    core/OsrmRoutingService.cpp \
    core/PathFinder.cpp \
    core/RealNavigationService.cpp \
    core/RoadGraph.cpp \
    core/TerminalLocationService.cpp \
    core/TileMapProvider.cpp \
    core/VoiceClient.cpp \
    pages/CallPage.cpp \
    pages/ChatHomePage.cpp \
    pages/ChatReplyPage.cpp \
    pages/EnvMonitorPage.cpp \
    pages/MapHomePage.cpp \
    pages/RouteDetailPage.cpp \
    pages/RoutePlanPage.cpp \
    widgets/HeaderBar.cpp \
    widgets/MapWidget.cpp \
    widgets/POICard.cpp \
    widgets/RouteStepItem.cpp \
    widgets/VoiceButton.cpp

HEADERS += \
    MainWindow.h \
    core/BackendConfig.h \
    core/CallClient.h \
    core/Haversine.h \
    core/MockChatService.h \
    core/MockDataService.h \
    core/NavigationController.h \
    core/SerialEnvReader.h \
    core/OsrmRoutingService.h \
    core/PathFinder.h \
    core/RealNavigationService.h \
    core/RoadGraph.h \
    core/TerminalLocationService.h \
    core/TileMapProvider.h \
    core/VoiceClient.h \
    models/ChatMessage.h \
    models/POIInfo.h \
    models/RouteInfo.h \
    pages/CallPage.h \
    pages/ChatHomePage.h \
    pages/ChatReplyPage.h \
    pages/EnvMonitorPage.h \
    pages/MapHomePage.h \
    pages/RouteDetailPage.h \
    pages/RoutePlanPage.h \
    widgets/HeaderBar.h \
    widgets/MapWidget.h \
    widgets/POICard.h \
    widgets/RouteStepItem.h \
    widgets/VoiceButton.h

RESOURCES += resources.qrc
