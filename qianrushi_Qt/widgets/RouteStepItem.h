/*
 * 文件：RouteStepItem.h
 * 说明：导航步骤列表项控件，显示步骤编号、标题和指引文字
 */

#pragma once

#include <QFrame>

#include "models/RouteInfo.h"

class QLabel;

/*
 * 导航步骤列表项控件
 * 显示导航路径中每一步的编号徽标、标题和详细指引，支持选中高亮和点击交互
 */
class RouteStepItem : public QFrame
{
    Q_OBJECT

public:
    explicit RouteStepItem(QWidget *parent = nullptr);

    /* 设置当前步骤数据（编号、标题、指引文字） */
    void setStep(const RouteStep &step);
    /* 设置选中状态，高亮显示当前步骤 */
    void setSelected(bool selected);
    /* 获取当前步骤的索引编号 */
    int stepIndex() const;

signals:
    /* 用户点击该步骤项时发射，携带步骤索引 */
    void clicked(int stepIndex);

protected:
    /* 自绘事件：绘制左侧连接线和选中高亮条 */
    void paintEvent(QPaintEvent *event) override;
    /* 鼠标按下事件：响应点击并发射 clicked 信号 */
    void mousePressEvent(QMouseEvent *event) override;

private:
    RouteStep m_step;               /* 当前步骤数据 */
    QLabel *m_badge = nullptr;      /* 步骤编号徽标 */
    QLabel *m_title = nullptr;      /* 步骤标题 */
    QLabel *m_instruction = nullptr; /* 步骤详细指引文字 */
};
