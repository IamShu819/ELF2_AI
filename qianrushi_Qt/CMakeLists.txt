cmake_minimum_required(VERSION 3.21)
project(SmartNavTerminal LANGUAGES CXX)

set(CMAKE_CXX_STANDARD 17)
set(CMAKE_CXX_STANDARD_REQUIRED ON)
set(CMAKE_AUTOMOC ON)
set(CMAKE_AUTORCC ON)
set(CMAKE_AUTOUIC ON)

if (WIN32 AND NOT CMAKE_PREFIX_PATH)
    set(CMAKE_PREFIX_PATH "D:/QT/6.11.1/mingw_64")
endif ()

find_package(Qt6 REQUIRED COMPONENTS Core Gui Widgets Svg Network Multimedia SerialPort)

set(PROJECT_SOURCES
    main.cpp
    MainWindow.h
    MainWindow.cpp
    resources.qrc
    core/NavigationController.h
    core/NavigationController.cpp
    core/SerialEnvReader.h
    core/SerialEnvReader.cpp
    core/BackendConfig.h
    core/BackendConfig.cpp
    core/MockDataService.h
    core/MockDataService.cpp
    core/MockChatService.h
    core/MockChatService.cpp
    core/VoiceClient.h
    core/VoiceClient.cpp
    core/CallClient.h
    core/CallClient.cpp
    core/TileMapProvider.h
    core/TileMapProvider.cpp
    core/TerminalLocationService.h
    core/TerminalLocationService.cpp
    core/Haversine.h
    core/RoadGraph.h
    core/RoadGraph.cpp
    core/PathFinder.h
    core/PathFinder.cpp
    core/RealNavigationService.h
    core/RealNavigationService.cpp
    core/OsrmRoutingService.h
    core/OsrmRoutingService.cpp
    models/POIInfo.h
    models/RouteInfo.h
    models/ChatMessage.h
    widgets/HeaderBar.h
    widgets/HeaderBar.cpp
    widgets/VoiceButton.h
    widgets/VoiceButton.cpp
    widgets/MapWidget.h
    widgets/MapWidget.cpp
    widgets/POICard.h
    widgets/POICard.cpp
    widgets/RouteStepItem.h
    widgets/RouteStepItem.cpp
    pages/ChatHomePage.h
    pages/ChatHomePage.cpp
    pages/ChatReplyPage.h
    pages/ChatReplyPage.cpp
    pages/MapHomePage.h
    pages/MapHomePage.cpp
    pages/RoutePlanPage.h
    pages/RoutePlanPage.cpp
    pages/RouteDetailPage.h
    pages/RouteDetailPage.cpp
    pages/EnvMonitorPage.h
    pages/EnvMonitorPage.cpp
    pages/CallPage.h
    pages/CallPage.cpp
)

add_executable(qianrushi ${PROJECT_SOURCES})

target_include_directories(qianrushi PRIVATE ${CMAKE_CURRENT_SOURCE_DIR})

target_link_libraries(qianrushi PRIVATE Qt6::Core Qt6::Gui Qt6::Widgets Qt6::Svg Qt6::Network Qt6::Multimedia Qt6::SerialPort)

if (WIN32 AND NOT DEFINED CMAKE_TOOLCHAIN_FILE)
    set(DEBUG_SUFFIX)
    if (MSVC AND CMAKE_BUILD_TYPE MATCHES "Debug")
        set(DEBUG_SUFFIX "d")
    endif ()
    set(QT_INSTALL_PATH "${CMAKE_PREFIX_PATH}")
    if (NOT EXISTS "${QT_INSTALL_PATH}/bin")
        set(QT_INSTALL_PATH "${QT_INSTALL_PATH}/..")
        if (NOT EXISTS "${QT_INSTALL_PATH}/bin")
            set(QT_INSTALL_PATH "${QT_INSTALL_PATH}/..")
        endif ()
    endif ()
    if (EXISTS "${QT_INSTALL_PATH}/plugins/platforms/qwindows${DEBUG_SUFFIX}.dll")
        add_custom_command(TARGET qianrushi POST_BUILD
            COMMAND ${CMAKE_COMMAND} -E make_directory
            "$<TARGET_FILE_DIR:qianrushi>/plugins/platforms/")
        add_custom_command(TARGET qianrushi POST_BUILD
            COMMAND ${CMAKE_COMMAND} -E copy
            "${QT_INSTALL_PATH}/plugins/platforms/qwindows${DEBUG_SUFFIX}.dll"
            "$<TARGET_FILE_DIR:qianrushi>/plugins/platforms/")
    endif ()
    foreach (QT_LIB Core Gui Widgets Svg Network Multimedia SerialPort)
        add_custom_command(TARGET qianrushi POST_BUILD
            COMMAND ${CMAKE_COMMAND} -E copy
            "${QT_INSTALL_PATH}/bin/Qt6${QT_LIB}${DEBUG_SUFFIX}.dll"
            "$<TARGET_FILE_DIR:qianrushi>")
    endforeach ()
    if (EXISTS "${QT_INSTALL_PATH}/plugins/multimedia")
        add_custom_command(TARGET qianrushi POST_BUILD
            COMMAND ${CMAKE_COMMAND} -E copy_directory
            "${QT_INSTALL_PATH}/plugins/multimedia"
            "$<TARGET_FILE_DIR:qianrushi>/plugins/multimedia")
    endif ()
    foreach (MINGW_DLL libgcc_s_seh-1.dll libstdc++-6.dll libwinpthread-1.dll)
        if (EXISTS "D:/QT/Tools/mingw1310_64/bin/${MINGW_DLL}")
            add_custom_command(TARGET qianrushi POST_BUILD
                COMMAND ${CMAKE_COMMAND} -E copy
                "D:/QT/Tools/mingw1310_64/bin/${MINGW_DLL}"
                "$<TARGET_FILE_DIR:qianrushi>")
        endif ()
    endforeach ()
    if (EXISTS "${CMAKE_CURRENT_SOURCE_DIR}/tiles")
        add_custom_command(TARGET qianrushi POST_BUILD
            COMMAND ${CMAKE_COMMAND} -E copy_directory
            "${CMAKE_CURRENT_SOURCE_DIR}/tiles"
            "$<TARGET_FILE_DIR:qianrushi>/tiles")
    endif ()
endif ()
