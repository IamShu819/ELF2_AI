/*
 * 文件：MapWidget.cpp
 * 说明：地图显示控件的完整实现
 */

#include "MapWidget.h"

#include "core/TerminalLocationService.h"

#include <QMouseEvent>
#include <QPainter>
#include <QPainterPath>
#include <QtMath>
#include <QVariantAnimation>

/*
 * 构造函数：初始化地图控件，设置定位脉冲动画和路径绘制动画
 */
MapWidget::MapWidget(QWidget *parent)
    : QWidget(parent)
{
    setMouseTracking(true);
    setMinimumSize(460, 360);
    setSizePolicy(QSizePolicy::Expanding, QSizePolicy::Expanding);
    const TerminalLocation cachedLocation = TerminalLocationService::cachedLocation();
    m_terminalGeoCoordinate = cachedLocation.geoCoordinate;
    m_terminalName = cachedLocation.name;
    m_terminalIsFallback = cachedLocation.isFallback;
    m_tileProvider.setCenterGeoCoordinate(m_terminalGeoCoordinate);

    /* 创建定位点脉冲动画（循环） */
    m_locationAnimation = new QVariantAnimation(this);
    m_locationAnimation->setStartValue(0.0);
    m_locationAnimation->setEndValue(1.0);
    m_locationAnimation->setDuration(1500);
    m_locationAnimation->setLoopCount(-1);
    connect(m_locationAnimation, &QVariantAnimation::valueChanged, this, [this](const QVariant &value) {
        setLocationPulse(value.toReal());
    });
    m_locationAnimation->start();

    /* 创建路径绘制动画（单次） */
    m_routeAnimation = new QVariantAnimation(this);
    m_routeAnimation->setStartValue(0.0);
    m_routeAnimation->setEndValue(1.0);
    m_routeAnimation->setDuration(900);
    connect(m_routeAnimation, &QVariantAnimation::valueChanged, this, [this](const QVariant &value) {
        setRouteProgress(value.toReal());
    });
}

/*
 * 设置兴趣点列表并刷新显示
 */
void MapWidget::setPOIs(const QList<POIInfo> &pois)
{
    m_pois = pois;
    update();
}

/*
 * 选中指定 ID 的兴趣点，高亮显示
 */
void MapWidget::setSelectedPOI(const QString &poiId)
{
    m_selectedPoiId = poiId;
    update();
}

/*
 * 设置导航路径，重置路径进度并启动绘制动画
 */
void MapWidget::setRoute(const RouteInfo &route)
{
    m_route = route;
    m_hasRoute = true;
    m_routeProgress = 0.0;
    startRouteAnimation();
}

/*
 * 清除当前导航路径
 */
void MapWidget::clearRoute()
{
    m_hasRoute = false;
    m_currentStep = 0;
    m_routeProgress = 1.0;
    update();
}

/*
 * 设置当前导航步骤索引，刷新高亮的步骤节点
 */
void MapWidget::setCurrentStep(int stepIndex)
{
    m_currentStep = stepIndex;
    update();
}

/*
 * 仅显示指定类型的兴趣点
 */
void MapWidget::setShowOnlyType(POIType type)
{
    m_typeFilter = type;
    m_hasTypeFilter = true;
    update();
}

/*
 * 清除类型筛选，显示全部兴趣点
 */
void MapWidget::clearTypeFilter()
{
    m_hasTypeFilter = false;
    update();
}

/*
 * 启动路径绘制动画
 */
void MapWidget::startRouteAnimation()
{
    m_routeAnimation->stop();
    m_routeAnimation->start();
}

/*
 * 获取当前定位脉冲值
 */
qreal MapWidget::locationPulse() const
{
    return m_locationPulse;
}

/*
 * 设置定位脉冲值
 */
void MapWidget::setLocationPulse(qreal value)
{
    m_locationPulse = value;
    update();
}

/*
 * 获取路径绘制进度值
 */
qreal MapWidget::routeProgress() const
{
    return m_routeProgress;
}

/*
 * 设置路径绘制进度值
 */
void MapWidget::setRouteProgress(qreal value)
{
    m_routeProgress = value;
    update();
}

/*
 * 绘制事件：渲染地图底图、定位点、兴趣点和导航路径
 */
void MapWidget::paintEvent(QPaintEvent *)
{
    QPainter painter(this);
    painter.setRenderHint(QPainter::Antialiasing);

    /* 绘制地图背景圆角矩形 */
    const QRectF rect = mapRect();
    painter.setPen(Qt::NoPen);
    painter.setBrush(QColor("#EDF4FA"));
    painter.drawRoundedRect(rect, 16, 16);

    /* 裁剪区域后绘制瓦片底图 */
    painter.save();
    QPainterPath clipPath;
    clipPath.addRoundedRect(rect, 16, 16);
    painter.setClipPath(clipPath);
    drawTileBase(painter, rect);
    painter.restore();

    /* 绘制导航路径 */
    if (m_hasRoute) {
        drawRoute(painter);
    }

    /* 绘制终端定位点及脉冲波纹 */
    const QPointF terminal = toWidgetPoint(m_terminalGeoCoordinate);
    const qreal pulseRadius = 18 + 28 * m_locationPulse;
    painter.setPen(Qt::NoPen);
    painter.setBrush(QColor(30, 115, 248, int(90 * (1.0 - m_locationPulse))));
    painter.drawEllipse(terminal, pulseRadius, pulseRadius);
    painter.setBrush(QColor("#1E73F8"));
    painter.drawEllipse(terminal, 11, 11);
    painter.setPen(QPen(Qt::white, 3));
    painter.drawEllipse(terminal, 5, 5);

    /* 绘制定位点名称标签 */
    painter.setPen(QColor("#0B2A6F"));
    QFont terminalFont = painter.font();
    terminalFont.setPointSize(10);
    terminalFont.setBold(true);
    painter.setFont(terminalFont);
    painter.drawText(QRectF(terminal.x() - 70, terminal.y() + 16, 140, 24), Qt::AlignCenter, m_terminalIsFallback ? QStringLiteral("本终端位置") : m_terminalName);

    /* 绘制所有可见兴趣点 */
    for (const POIInfo &poi : visiblePOIs()) {
        const QPointF pos = toWidgetPoint(poi.hasGeoCoordinate ? poi.geoCoordinate : poi.mapPosition);
        const bool selected = poi.id == m_selectedPoiId;
        const int radius = selected ? 18 : 14;
        painter.setPen(QPen(Qt::white, selected ? 4 : 3));
        painter.setBrush(colorForType(poi.type));
        painter.drawEllipse(pos, radius, radius);
        painter.setPen(Qt::white);
        QFont iconFont = painter.font();
        iconFont.setBold(true);
        iconFont.setPointSize(selected ? 13 : 10);
        painter.setFont(iconFont);
        painter.drawText(QRectF(pos.x() - radius, pos.y() - radius, radius * 2, radius * 2), Qt::AlignCenter, iconForType(poi.type));

        /* 绘制兴趣点名称标签 */
        painter.setPen(QColor("#1D2939"));
        QFont labelFont = painter.font();
        labelFont.setPointSize(10);
        labelFont.setBold(selected);
        painter.setFont(labelFont);
        painter.drawText(QRectF(pos.x() - 64, pos.y() + radius + 5, 128, 24), Qt::AlignCenter, poi.name);

        /* 选中状态下绘制信息气泡 */
        if (selected) {
            QRectF bubble(pos.x() - 82, pos.y() - 74, 164, 44);
            painter.setPen(QPen(QColor("#D8E8FF"), 1));
            painter.setBrush(Qt::white);
            painter.drawRoundedRect(bubble, 12, 12);
            painter.setPen(QColor("#0B2A6F"));
            painter.drawText(bubble.adjusted(8, 4, -8, -22), Qt::AlignCenter, poi.name);
            painter.setPen(QColor("#667085"));
            painter.drawText(bubble.adjusted(8, 22, -8, -4), Qt::AlignCenter, QStringLiteral("%1 米 · %2 分钟").arg(poi.distanceMeters).arg(poi.durationMinutes));
        }
    }
}

/*
 * 鼠标按下事件：记录位置，为拖拽或点击做准备
 */
void MapWidget::mousePressEvent(QMouseEvent *event)
{
    m_pressPos = event->pos();
    m_lastDragPos = event->pos();
    m_dragging = false;
    if (event->button() == Qt::LeftButton && poiAt(event->pos()).id.isEmpty()) {
        setCursor(Qt::ClosedHandCursor);
    }
    QWidget::mousePressEvent(event);
}

/*
 * 鼠标释放事件：如果是点击（非拖拽）且落在兴趣点上，发射点击信号
 */
void MapWidget::mouseReleaseEvent(QMouseEvent *event)
{
    const POIInfo poi = poiAt(event->pos());
    if (event->button() == Qt::LeftButton && !m_dragging && !poi.id.isEmpty()) {
        m_selectedPoiId = poi.id;
        emit poiClicked(poi);
        update();
    }
    m_dragging = false;
    setCursor(poiAt(event->pos()).id.isEmpty() ? Qt::ArrowCursor : Qt::PointingHandCursor);
    QWidget::mouseReleaseEvent(event);
}

/*
 * 鼠标移动事件：拖拽地图平移或更新光标样式
 */
void MapWidget::mouseMoveEvent(QMouseEvent *event)
{
    if (event->buttons() & Qt::LeftButton) {
        const QPoint delta = event->pos() - m_lastDragPos;
        if ((event->pos() - m_pressPos).manhattanLength() > 4) {
            m_dragging = true;
        }
        if (m_dragging) {
            m_tileProvider.panByPixels(delta);
            m_lastDragPos = event->pos();
            setCursor(Qt::ClosedHandCursor);
            update();
            return;
        }
    }
    setCursor(poiAt(event->pos()).id.isEmpty() ? Qt::ArrowCursor : Qt::PointingHandCursor);
    QWidget::mouseMoveEvent(event);
}

/*
 * 鼠标离开事件：恢复默认光标
 */
void MapWidget::leaveEvent(QEvent *event)
{
    setCursor(Qt::ArrowCursor);
    QWidget::leaveEvent(event);
}

/*
 * 获取地图有效绘制区域（四周留出内边距）
 */
QRectF MapWidget::mapRect() const
{
    return rect().adjusted(12, 12, -12, -12);
}

/*
 * 将地图坐标（地理坐标或归一化坐标）转换为控件像素坐标
 */
QPointF MapWidget::toWidgetPoint(const QPointF &mapPoint) const
{
    if (mapPoint.x() > 1.0 || mapPoint.y() > 1.0 || mapPoint.x() < 0.0 || mapPoint.y() < 0.0) {
        const QPointF world = m_tileProvider.worldPixelForGeo(mapPoint);
        const QPointF center = m_tileProvider.centerWorldPixel();
        return mapRect().center() + (world - center);
    }
    const QRectF r = mapRect().adjusted(34, 34, -34, -34);
    return QPointF(r.left() + r.width() * mapPoint.x(), r.top() + r.height() * mapPoint.y());
}

/*
 * 绘制瓦片底图
 * 计算当前视图范围内的瓦片，逐块绘制并叠加半透明遮罩
 */
void MapWidget::drawTileBase(QPainter &painter, const QRectF &rect)
{
    const int zoom = m_tileProvider.zoom();
    const int tileSize = m_tileProvider.tileSize();
    const QPointF centerWorld = m_tileProvider.centerWorldPixel();
    const QPointF topLeftWorld(centerWorld.x() - rect.width() / 2.0, centerWorld.y() - rect.height() / 2.0);
    const int startX = qFloor(topLeftWorld.x() / tileSize);
    const int startY = qFloor(topLeftWorld.y() / tileSize);
    const int endX = qFloor((topLeftWorld.x() + rect.width()) / tileSize);
    const int endY = qFloor((topLeftWorld.y() + rect.height()) / tileSize);
    const int maxTile = 1 << zoom;

    /* 逐行逐列绘制瓦片 */
    for (int ty = startY; ty <= endY + 1; ++ty) {
        if (ty < 0 || ty >= maxTile) {
            continue;
        }
        for (int tx = startX; tx <= endX + 1; ++tx) {
            const int wrappedX = ((tx % maxTile) + maxTile) % maxTile;
            const QPixmap pixmap = m_tileProvider.tile(zoom, wrappedX, ty);
            const qreal screenX = rect.left() + tx * tileSize - topLeftWorld.x();
            const qreal screenY = rect.top() + ty * tileSize - topLeftWorld.y();
            painter.drawPixmap(QRectF(screenX, screenY, tileSize, tileSize), pixmap, QRectF(0, 0, tileSize, tileSize));
        }
    }

    /* 叠加半透明遮罩和边框 */
    painter.fillRect(rect, QColor(255, 255, 255, 24));
    painter.setPen(QPen(QColor("#C8D8EA"), 1));
    painter.setBrush(Qt::NoBrush);
    painter.drawRoundedRect(rect.adjusted(0.5, 0.5, -0.5, -0.5), 16, 16);

    /* 左上角显示位置和缩放级别 */
    painter.setPen(QColor("#64748B"));
    QFont font = painter.font();
    font.setPointSize(9);
    font.setWeight(QFont::DemiBold);
    painter.setFont(font);
    painter.drawText(rect.adjusted(14, 12, -14, -12), Qt::AlignLeft | Qt::AlignTop,
                     QStringLiteral("武汉市洪山区 · 瓦片底图 · Z%1").arg(zoom));

    /* 右下角显示底图版权信息 */
    const QString attribution = m_tileProvider.attribution();
    const QFontMetrics metrics(font);
    const QRectF attrRect(rect.right() - metrics.horizontalAdvance(attribution) - 22,
                          rect.bottom() - 28,
                          metrics.horizontalAdvance(attribution) + 12,
                          20);
    painter.setPen(Qt::NoPen);
    painter.setBrush(QColor(255, 255, 255, 210));
    painter.drawRoundedRect(attrRect, 6, 6);
    painter.setPen(QColor("#475569"));
    painter.drawText(attrRect, Qt::AlignCenter, attribution);
}

/*
 * 获取当前可见的兴趣点列表
 * 若启用了类型筛选，则只返回匹配类型的兴趣点
 */
QList<POIInfo> MapWidget::visiblePOIs() const
{
    if (!m_hasTypeFilter) {
        return m_pois;
    }
    QList<POIInfo> result;
    for (const POIInfo &poi : m_pois) {
        if (poi.type == m_typeFilter) {
            result.append(poi);
        }
    }
    return result;
}

/*
 * 获取指定像素坐标处的兴趣点
 * 遍历可见兴趣点，返回距离在阈值内的第一个匹配项
 */
POIInfo MapWidget::poiAt(const QPoint &pos) const
{
    for (const POIInfo &poi : visiblePOIs()) {
        const QPointF point = toWidgetPoint(poi.hasGeoCoordinate ? poi.geoCoordinate : poi.mapPosition);
        if (QLineF(point, pos).length() <= 22) {
            return poi;
        }
    }
    return {};
}

/*
 * 根据兴趣点类型返回对应的标注颜色
 */
QColor MapWidget::colorForType(POIType type) const
{
    switch (type) {
    case POIType::Park:
        return QColor("#22C55E");
    case POIType::Hospital:
        return QColor("#EF4444");
    case POIType::BusStation:
        return QColor("#4A90FF");
    case POIType::Toilet:
        return QColor("#14B8A6");
    case POIType::Government:
        return QColor("#F59E0B");
    case POIType::ServiceCenter:
        return QColor("#7C3AED");
    case POIType::Other:
        return QColor("#667085");
    }
    return QColor("#667085");
}

/*
 * 根据兴趣点类型返回对应的图标显示文字
 */
QString MapWidget::iconForType(POIType type) const
{
    switch (type) {
    case POIType::Park:
        return QStringLiteral("园");
    case POIType::Hospital:
        return QStringLiteral("医");
    case POIType::BusStation:
        return QStringLiteral("车");
    case POIType::Toilet:
        return QStringLiteral("卫");
    case POIType::Government:
        return QStringLiteral("政");
    case POIType::ServiceCenter:
        return QStringLiteral("服");
    case POIType::Other:
        return QStringLiteral("点");
    }
    return QStringLiteral("点");
}

/*
 * 绘制导航路径及步骤节点
 * 包含路径底色阴影、动画进度路径、以及各步骤编号圆圈
 */
void MapWidget::drawRoute(QPainter &painter)
{
    if (m_route.pathPoints.size() < 2) {
        return;
    }

    painter.save();
    QPainterPath clipPath;
    clipPath.addRoundedRect(mapRect(), 16, 16);
    painter.setClipPath(clipPath);
    painter.setBrush(Qt::NoBrush);

    /* 将路径点转换为控件坐标 */
    QVector<QPointF> points;
    for (const QPointF &point : m_route.pathPoints) {
        points.append(toWidgetPoint(point));
    }

    /* 构建完整路径 */
    QPainterPath fullPath(points.first());
    for (int i = 1; i < points.size(); ++i) {
        fullPath.lineTo(points[i]);
    }

    /* 绘制路径底色阴影 */
    painter.setPen(QPen(QColor(30, 115, 248, 50), 14, Qt::SolidLine, Qt::RoundCap, Qt::RoundJoin));
    painter.drawPath(fullPath);

    /* 计算各段路径长度，根据进度值绘制动画路径 */
    QVector<double> segmentLengths;
    double totalLength = 0.0;
    for (int i = 1; i < points.size(); ++i) {
        const double length = QLineF(points[i - 1], points[i]).length();
        segmentLengths.append(length);
        totalLength += length;
    }

    const double targetLength = totalLength * qBound<qreal>(0.0, m_routeProgress, 1.0);
    double drawnLength = 0.0;
    QPainterPath animatedPath(points.first());
    for (int i = 1; i < points.size(); ++i) {
        const double segmentLength = segmentLengths.value(i - 1);
        if (drawnLength + segmentLength <= targetLength) {
            animatedPath.lineTo(points[i]);
            drawnLength += segmentLength;
            continue;
        }
        if (segmentLength > 0.0 && drawnLength < targetLength) {
            const double ratio = (targetLength - drawnLength) / segmentLength;
            animatedPath.lineTo(points[i - 1] + (points[i] - points[i - 1]) * ratio);
        }
        break;
    }

    /* 绘制路径主线条 */
    painter.setPen(QPen(QColor("#1E73F8"), 7, Qt::SolidLine, Qt::RoundCap, Qt::RoundJoin));
    painter.drawPath(animatedPath);

    /* 绘制各步骤节点圆圈 */
    for (const RouteStep &step : m_route.steps) {
        const QPointF pos = toWidgetPoint(step.point);
        const bool active = step.index == m_currentStep;
        painter.setPen(QPen(Qt::white, active ? 4 : 3));
        painter.setBrush(active ? QColor("#F59E0B") : QColor("#1E73F8"));
        painter.drawEllipse(pos, active ? 15 : 11, active ? 15 : 11);
        painter.setPen(Qt::white);
        QFont font = painter.font();
        font.setBold(true);
        font.setPointSize(10);
        painter.setFont(font);
        painter.drawText(QRectF(pos.x() - 15, pos.y() - 15, 30, 30), Qt::AlignCenter, QString::number(step.index));
    }

    painter.restore();
}
