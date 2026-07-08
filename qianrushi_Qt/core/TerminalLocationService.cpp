/*!
 * @file       TerminalLocationService.cpp
 * @brief      终端定位服务实现，支持 Windows 运行时和 .NET 定位
 */

#include "TerminalLocationService.h"

#include <QDateTime>
#include <QProcess>
#include <QRegularExpression>

namespace {

/*! 返回默认兜底位置（武汉职业技术学院东区） */
TerminalLocation fallbackLocation()
{
    return {
        QStringLiteral("武汉职业技术学院东区工业中心"),
        QStringLiteral("fallback"),
        QPointF(114.410465, 30.4865333),
        true,
        0.0
    };
}

/*!
 * 解析定位提供程序的输出文本
 * 格式：来源;经度;纬度;精度
 * @param output 定位脚本输出
 * @return 解析后的位置信息，解析失败时返回兜底位置
 */
TerminalLocation parseProviderOutput(const QString &output)
{
    const QRegularExpression pattern(QStringLiteral(R"(^\s*([^;]+);(-?\d+(?:\.\d+)?);(-?\d+(?:\.\d+)?);(-?\d+(?:\.\d+)?)\s*$)"));
    const QRegularExpressionMatch match = pattern.match(output.trimmed());
    if (!match.hasMatch()) {
        return fallbackLocation();
    }

    bool okLon = false;
    bool okLat = false;
    bool okAccuracy = false;
    const double lon = match.captured(2).toDouble(&okLon);
    const double lat = match.captured(3).toDouble(&okLat);
    const double accuracy = match.captured(4).toDouble(&okAccuracy);
    if (!okLon || !okLat || !okAccuracy || lon < -180.0 || lon > 180.0 || lat < -90.0 || lat > 90.0) {
        return fallbackLocation();
    }

    const QString source = match.captured(1).trimmed();
    return {
        QStringLiteral("系统定位位置"),
        source,
        QPointF(lon, lat),
        false,
        accuracy
    };
}

/*!
 * 执行 PowerShell 定位脚本并返回结果
 * @param script PowerShell 脚本内容
 * @param timeoutMs 超时时间（毫秒）
 * @return 定位结果
 */
TerminalLocation runPowerShellLocationProvider(const QString &script, int timeoutMs)
{
    QProcess process;
    process.start(QStringLiteral("powershell"),
                  {QStringLiteral("-NoProfile"),
                   QStringLiteral("-ExecutionPolicy"),
                   QStringLiteral("Bypass"),
                   QStringLiteral("-Command"),
                   script});
    if (!process.waitForFinished(timeoutMs) || process.exitStatus() != QProcess::NormalExit || process.exitCode() != 0) {
        process.kill();
        process.waitForFinished(200);
        return fallbackLocation();
    }

    return parseProviderOutput(QString::fromLocal8Bit(process.readAllStandardOutput()));
}

/*!
 * 使用 Windows.Devices.Geolocation API 获取定位
 * @return 定位结果
 */
TerminalLocation windowsRuntimeLocation()
{
#ifdef Q_OS_WIN
    const QString script = QStringLiteral(
        "Add-Type -AssemblyName System.Runtime.WindowsRuntime;"
        "$locator = [Windows.Devices.Geolocation.Geolocator,Windows.Devices.Geolocation,ContentType=WindowsRuntime]::new();"
        "$locator.DesiredAccuracy = [Windows.Devices.Geolocation.PositionAccuracy]::High;"
        "$op = $locator.GetGeopositionAsync();"
        "$method = [System.WindowsRuntimeSystemExtensions].GetMethods() | Where-Object {"
        "  $_.Name -eq 'AsTask' -and $_.IsGenericMethodDefinition -and $_.GetGenericArguments().Count -eq 1 -and $_.GetParameters().Count -eq 1"
        "} | Select-Object -First 1;"
        "$task = $method.MakeGenericMethod([Windows.Devices.Geolocation.Geoposition]).Invoke($null, @($op));"
        "if ($task.Wait(8000)) {"
        "  $coord = $task.Result.Coordinate;"
        "  $pos = $coord.Point.Position;"
        "  $culture = [System.Globalization.CultureInfo]::InvariantCulture;"
        "  [string]::Format($culture, 'winrt;{0};{1};{2}', $pos.Longitude, $pos.Latitude, $coord.Accuracy)"
        "} else { 'UNAVAILABLE' }");

    return runPowerShellLocationProvider(script, 9500);
#else
    return fallbackLocation();
#endif
}

/*!
 * 使用 System.Device.Location API 获取定位
 * @return 定位结果
 */
TerminalLocation dotNetLocation()
{
#ifdef Q_OS_WIN
    const QString script = QStringLiteral(
        "Add-Type -AssemblyName System.Device;"
        "$watcher = New-Object System.Device.Location.GeoCoordinateWatcher([System.Device.Location.GeoPositionAccuracy]::High);"
        "$started = $watcher.TryStart($false, [TimeSpan]::FromMilliseconds(3000));"
        "if ($started -and !$watcher.Position.Location.IsUnknown) {"
        "  $culture = [System.Globalization.CultureInfo]::InvariantCulture;"
        "  [string]::Format($culture, 'dotnet;{0};{1};{2}',"
        "    $watcher.Position.Location.Longitude,"
        "    $watcher.Position.Location.Latitude,"
        "    $watcher.Position.Location.HorizontalAccuracy)"
        "} else { 'UNAVAILABLE' }");

    return runPowerShellLocationProvider(script, 4000);
#else
    return fallbackLocation();
#endif
}

/*!
 * 获取系统定位，依次尝试 Windows 运行时定位和 .NET 定位
 * @return 定位结果，全部失败时返回兜底位置
 */
TerminalLocation systemLocation()
{
    TerminalLocation location = windowsRuntimeLocation();
    if (!location.isFallback) {
        return location;
    }

    location = dotNetLocation();
    if (!location.isFallback) {
        return location;
    }

    return fallbackLocation();
}

TerminalLocation &cachedLocationState()
{
    static TerminalLocation cachedLocation = fallbackLocation();
    return cachedLocation;
}

qint64 &lastAttemptAtState()
{
    static qint64 lastAttemptAt = 0;
    return lastAttemptAt;
}

}

/*!
 * 获取当前终端位置
 * 使用一分钟内缓存策略，避免频繁调用系统定位接口
 * @return 当前终端位置信息
 */
TerminalLocation TerminalLocationService::currentLocation()
{
    TerminalLocation &cachedLocation = cachedLocationState();
    qint64 &lastAttemptAt = lastAttemptAtState();

    const qint64 now = QDateTime::currentMSecsSinceEpoch();
    if (lastAttemptAt != 0 && now - lastAttemptAt < 60000) {
        return cachedLocation;
    }

    lastAttemptAt = now;
    cachedLocation = systemLocation();
    return cachedLocation;
}

TerminalLocation TerminalLocationService::cachedLocation()
{
    return cachedLocationState();
}
