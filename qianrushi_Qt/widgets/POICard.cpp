/*
 * 文件：POICard.cpp
 * 说明：兴趣点信息卡片控件的实现
 */

#include "POICard.h"

#include <QLabel>
#include <QPushButton>
#include <QVBoxLayout>

/*
 * 构造函数：初始化卡片布局
 * 包含兴趣点名称、元信息、描述文本和"去这里"导航按钮
 */
POICard::POICard(QWidget *parent)
    : QFrame(parent)
{
    setObjectName(QStringLiteral("MiniCard"));
    auto *layout = new QVBoxLayout(this);
    layout->setContentsMargins(16, 14, 16, 14);
    layout->setSpacing(8);

    /* 创建信息标签 */
    m_name = new QLabel(this);
    m_name->setObjectName(QStringLiteral("CardTitle"));
    m_meta = new QLabel(this);
    m_meta->setObjectName(QStringLiteral("MetaText"));
    m_desc = new QLabel(this);
    m_desc->setObjectName(QStringLiteral("BodyText"));
    m_desc->setWordWrap(true);
    /* 导航按钮 */
    auto *button = new QPushButton(QStringLiteral("去这里"), this);
    button->setObjectName(QStringLiteral("PrimaryButton"));
    button->setCursor(Qt::PointingHandCursor);

    layout->addWidget(m_name);
    layout->addWidget(m_meta);
    layout->addWidget(m_desc);
    layout->addWidget(button);

    /* 点击"去这里"时发射导航请求信号 */
    connect(button, &QPushButton::clicked, this, [this]() {
        emit routeRequested(m_poi);
    });
}

/*
 * 设置兴趣点信息
 * 更新名称、元信息（距离/时长/类型）和描述文本
 */
void POICard::setPOI(const POIInfo &poi)
{
    m_poi = poi;
    m_name->setText(poi.name);
    m_meta->setText(QStringLiteral("约 %1 米 · %2 分钟 · %3").arg(poi.distanceMeters).arg(poi.durationMinutes).arg(poiTypeName(poi.type)));
    m_desc->setText(poi.description);
}
