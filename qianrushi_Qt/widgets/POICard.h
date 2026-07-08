/*
 * 文件：POICard.h
 * 说明：兴趣点信息卡片控件，展示名称、距离、时长和描述
 */

#pragma once

#include <QFrame>

#include "models/POIInfo.h"

class QLabel;

/*
 * 兴趣点信息卡片控件
 * 显示兴趣点的名称、距离/时长元信息、详细描述，并提供"去这里"导航按钮
 */
class POICard : public QFrame
{
    Q_OBJECT

public:
    explicit POICard(QWidget *parent = nullptr);

    /* 设置要显示的兴趣点信息 */
    void setPOI(const POIInfo &poi);

signals:
    /* 用户点击"去这里"按钮时发射，请求导航至此兴趣点 */
    void routeRequested(const POIInfo &poi);

private:
    POIInfo m_poi;              /* 当前显示的兴趣点数据 */
    QLabel *m_name = nullptr;   /* 兴趣点名称标签 */
    QLabel *m_meta = nullptr;   /* 距离、时长、类型等元信息标签 */
    QLabel *m_desc = nullptr;   /* 兴趣点详细描述标签 */
};
