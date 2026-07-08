/*!
 * @file       TileMapProvider.cpp
 * @brief      瓦片地图提供者实现
 */

#include "TileMapProvider.h"

#include <QCoreApplication>
#include <QDir>
#include <QFileInfo>
#include <QPainter>
#include <QPainterPath>
#include <QtMath>

#include <cmath>

/*! 构造函数，初始化地图中心和本地瓦片目录 */
TileMapProvider::TileMapProvider()
{
    m_centerGeoCoordinate = QPointF(114.410465, 30.4865333);
    m_centerWorldPixel = lonLatToWorldPixel(m_centerGeoCoordinate.x(), m_centerGeoCoordinate.y(), m_zoom, m_tileSize);
    m_localTileRoot = QDir(QCoreApplication::applicationDirPath()).filePath(QStringLiteral("tiles"));
}

/*! 获取当前缩放级别 */
int TileMapProvider::zoom() const
{
    return m_zoom;
}

/*! 获取瓦片像素尺寸 */
int TileMapProvider::tileSize() const
{
    return m_tileSize;
}

/*! 获取地图归属信息 */
QString TileMapProvider::attribution() const
{
    return QStringLiteral("© OpenStreetMap contributors");
}

/*! 获取地图中心经纬度坐标 */
QPointF TileMapProvider::centerGeoCoordinate() const
{
    return m_centerGeoCoordinate;
}

/*! 获取地图中心世界像素坐标 */
QPointF TileMapProvider::centerWorldPixel() const
{
    return m_centerWorldPixel;
}

/*!
 * 将经纬度坐标转换为世界像素坐标
 * @param geoCoordinate 经纬度坐标
 * @return 世界像素坐标
 */
QPointF TileMapProvider::worldPixelForGeo(const QPointF &geoCoordinate) const
{
    return lonLatToWorldPixel(geoCoordinate.x(), geoCoordinate.y(), m_zoom, m_tileSize);
}

/*!
 * 将世界像素坐标转换为经纬度坐标
 * @param worldPixel 世界像素坐标
 * @return 经纬度坐标
 */
QPointF TileMapProvider::geoForWorldPixel(const QPointF &worldPixel) const
{
    return worldPixelToLonLat(worldPixel, m_zoom, m_tileSize);
}

/*!
 * 设置地图中心经纬度坐标
 * @param geoCoordinate 目标经纬度坐标
 */
void TileMapProvider::setCenterGeoCoordinate(const QPointF &geoCoordinate)
{
    m_centerGeoCoordinate = geoCoordinate;
    m_centerWorldPixel = lonLatToWorldPixel(geoCoordinate.x(), geoCoordinate.y(), m_zoom, m_tileSize);
}

/*!
 * 按像素偏移量平移地图
 * @param deltaPixels 像素偏移量
 */
void TileMapProvider::panByPixels(const QPointF &deltaPixels)
{
    m_centerWorldPixel -= deltaPixels;
    m_centerGeoCoordinate = worldPixelToLonLat(m_centerWorldPixel, m_zoom, m_tileSize);
}

/*!
 * 获取指定缩放级别和瓦片坐标的瓦片图像
 * @param zoom 缩放级别
 * @param x 瓦片列号
 * @param y 瓦片行号
 * @return 瓦片图像，加载失败时返回占位图
 */
QPixmap TileMapProvider::tile(int zoom, int x, int y) const
{
    const QString key = tileKey(zoom, x, y);
    const auto cached = m_cache.constFind(key);
    if (cached != m_cache.constEnd()) {
        return cached.value();
    }

    QPixmap pixmap = loadTileFromDisk(zoom, x, y);
    if (pixmap.isNull()) {
        pixmap = makeFallbackTile(zoom, x, y);
    }
    m_cache.insert(key, pixmap);
    return pixmap;
}

/*!
 * 经纬度转世界像素坐标（墨卡托投影）
 * @param lon 经度
 * @param lat 纬度（限制在 ±85.05112878 范围内）
 * @param zoom 缩放级别
 * @param tileSize 瓦片尺寸
 * @return 世界像素坐标
 */
QPointF TileMapProvider::lonLatToWorldPixel(double lon, double lat, int zoom, int tileSize)
{
    const double sinLat = qSin(qDegreesToRadians(qBound(-85.05112878, lat, 85.05112878)));
    const double n = qPow(2.0, zoom) * tileSize;
    const double x = (lon + 180.0) / 360.0 * n;
    const double y = (0.5 - qLn((1.0 + sinLat) / (1.0 - sinLat)) / (4.0 * M_PI)) * n;
    return QPointF(x, y);
}

/*!
 * 世界像素坐标转经纬度（墨卡托投影反算）
 * @param worldPixel 世界像素坐标
 * @param zoom 缩放级别
 * @param tileSize 瓦片尺寸
 * @return 经纬度坐标
 */
QPointF TileMapProvider::worldPixelToLonLat(const QPointF &worldPixel, int zoom, int tileSize)
{
    const double n = qPow(2.0, zoom) * tileSize;
    const double lon = worldPixel.x() / n * 360.0 - 180.0;
    const double mercator = M_PI * (1.0 - 2.0 * worldPixel.y() / n);
    const double lat = qRadiansToDegrees(qAtan(std::sinh(mercator)));
    return QPointF(lon, lat);
}

/*!
 * 从本地磁盘加载瓦片图像
 * @param zoom 缩放级别
 * @param x 瓦片列号
 * @param y 瓦片行号
 * @return 瓦片图像，文件不存在时返回空图像
 */
QPixmap TileMapProvider::loadTileFromDisk(int zoom, int x, int y) const
{
    const QString path = QDir(m_localTileRoot)
        .filePath(QStringLiteral("%1/%2/%3.png").arg(zoom).arg(x).arg(y));
    if (!QFileInfo::exists(path)) {
        return {};
    }
    QPixmap pixmap(path);
    if (!pixmap.isNull() && pixmap.size() != QSize(m_tileSize, m_tileSize)) {
        pixmap = pixmap.scaled(m_tileSize, m_tileSize, Qt::IgnoreAspectRatio, Qt::SmoothTransformation);
    }
    return pixmap;
}

/*!
 * 生成瓦片缺失时的占位图像（仿地图风格）
 * @param zoom 缩放级别
 * @param x 瓦片列号
 * @param y 瓦片行号
 * @return 占位瓦片图像
 */
QPixmap TileMapProvider::makeFallbackTile(int zoom, int x, int y) const
{
    QPixmap pixmap(m_tileSize, m_tileSize);
    pixmap.fill(QColor("#F2F6FA"));

    QPainter painter(&pixmap);
    painter.setRenderHint(QPainter::Antialiasing);

    const uint seed = qHash(QStringLiteral("%1/%2/%3").arg(zoom).arg(x).arg(y));
    const QColor park("#DDEFD8");
    const QColor water("#D8EAFB");
    const QColor block("#E9EEF5");
    const QColor roadEdge("#CED9E7");
    const QColor road("#FFFFFF");

    painter.setPen(Qt::NoPen);
    if ((seed % 5) == 0) {
        painter.setBrush(park);
        painter.drawRoundedRect(QRectF(18, 20, 94, 70), 16, 16);
    }
    if ((seed % 7) == 0) {
        painter.setBrush(water);
        QPainterPath waterPath;
        waterPath.moveTo(0, 180);
        waterPath.cubicTo(70, 154, 130, 204, 256, 168);
        waterPath.lineTo(256, 256);
        waterPath.lineTo(0, 256);
        waterPath.closeSubpath();
        painter.drawPath(waterPath);
    }

    painter.setBrush(block);
    for (int i = 0; i < 5; ++i) {
        const int bx = 20 + ((seed >> (i * 3)) % 180);
        const int by = 18 + ((seed >> (i * 4 + 2)) % 170);
        const int bw = 28 + ((seed >> (i + 1)) % 42);
        const int bh = 22 + ((seed >> (i + 5)) % 46);
        painter.drawRoundedRect(QRectF(bx, by, bw, bh), 8, 8);
    }

    auto drawRoad = [&](const QLineF &line, qreal edgeWidth, qreal roadWidth) {
        painter.setPen(QPen(roadEdge, edgeWidth, Qt::SolidLine, Qt::RoundCap, Qt::RoundJoin));
        painter.drawLine(line);
        painter.setPen(QPen(road, roadWidth, Qt::SolidLine, Qt::RoundCap, Qt::RoundJoin));
        painter.drawLine(line);
    };

    drawRoad(QLineF(-24, 68 + int(seed % 36), 280, 92 + int((seed >> 3) % 32)), 16, 11);
    drawRoad(QLineF(62 + int((seed >> 5) % 42), -20, 92 + int((seed >> 6) % 34), 280), 15, 10);
    if ((seed % 3) == 0) {
        drawRoad(QLineF(-20, 188, 280, 188 - int((seed >> 4) % 28)), 13, 8);
    }
    if ((seed % 4) == 0) {
        drawRoad(QLineF(178, -18, 194 - int((seed >> 7) % 26), 280), 12, 8);
    }

    painter.setPen(QPen(QColor("#DAE3EF"), 1));
    painter.drawRect(QRect(0, 0, m_tileSize - 1, m_tileSize - 1));
    painter.end();
    return pixmap;
}

/*!
 * 生成瓦片缓存键
 * @param zoom 缩放级别
 * @param x 瓦片列号
 * @param y 瓦片行号
 * @return 缓存键字符串
 */
QString TileMapProvider::tileKey(int zoom, int x, int y) const
{
    return QStringLiteral("%1/%2/%3").arg(zoom).arg(x).arg(y);
}
