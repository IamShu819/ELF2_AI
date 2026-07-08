/*!
 * @file       TileMapProvider.h
 * @brief      瓦片地图提供者，管理地图瓦片的加载、缓存与坐标转换
 */

#pragma once

#include <QHash>
#include <QPixmap>
#include <QPointF>
#include <QString>

/*! 瓦片地图提供者，负责经纬度与像素坐标的转换、瓦片加载和缓存 */
class TileMapProvider
{
public:
    TileMapProvider();

    /*! 获取当前缩放级别 */
    int zoom() const;
    /*! 获取瓦片像素尺寸 */
    int tileSize() const;
    /*! 获取地图归属信息 */
    QString attribution() const;
    /*! 获取地图中心经纬度坐标 */
    QPointF centerGeoCoordinate() const;
    /*! 获取地图中心世界像素坐标 */
    QPointF centerWorldPixel() const;
    /*! 将经纬度坐标转换为世界像素坐标 */
    QPointF worldPixelForGeo(const QPointF &geoCoordinate) const;
    /*! 将世界像素坐标转换为经纬度坐标 */
    QPointF geoForWorldPixel(const QPointF &worldPixel) const;
    /*! 设置地图中心经纬度坐标 */
    void setCenterGeoCoordinate(const QPointF &geoCoordinate);
    /*! 按像素偏移量平移地图 */
    void panByPixels(const QPointF &deltaPixels);
    /*! 获取指定缩放级别和瓦片坐标的瓦片图像 */
    QPixmap tile(int zoom, int x, int y) const;

private:
    /*! 经纬度转世界像素坐标（静态方法） */
    static QPointF lonLatToWorldPixel(double lon, double lat, int zoom, int tileSize);
    /*! 世界像素坐标转经纬度（静态方法） */
    static QPointF worldPixelToLonLat(const QPointF &worldPixel, int zoom, int tileSize);
    /*! 从本地磁盘加载瓦片 */
    QPixmap loadTileFromDisk(int zoom, int x, int y) const;
    /*! 生成瓦片缺失时的占位图像 */
    QPixmap makeFallbackTile(int zoom, int x, int y) const;
    /*! 生成瓦片缓存键 */
    QString tileKey(int zoom, int x, int y) const;

    int m_zoom = 16;                                    /*!< 当前缩放级别 */
    int m_tileSize = 256;                               /*!< 瓦片像素尺寸 */
    QPointF m_centerGeoCoordinate;                      /*!< 地图中心经纬度 */
    QPointF m_centerWorldPixel;                         /*!< 地图中心世界像素坐标 */
    QString m_localTileRoot;                            /*!< 本地瓦片文件根目录 */
    mutable QHash<QString, QPixmap> m_cache;            /*!< 瓦片图像缓存 */
};
