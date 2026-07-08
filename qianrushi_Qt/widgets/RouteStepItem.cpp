/*
 * 文件：RouteStepItem.cpp
 * 说明：导航步骤列表项控件的实现
 */

#include "RouteStepItem.h"

#include <QHBoxLayout>
#include <QLabel>
#include <QMouseEvent>
#include <QPainter>
#include <QStyle>
#include <QVBoxLayout>

/*
 * 构造函数：初始化步骤项布局
 * 包含左侧编号徽标、右侧标题和详细指引文字
 */
RouteStepItem::RouteStepItem(QWidget *parent)
    : QFrame(parent)
{
    setObjectName(QStringLiteral("RouteStepItem"));
    setCursor(Qt::PointingHandCursor);

    auto *layout = new QHBoxLayout(this);
    layout->setContentsMargins(14, 12, 14, 12);
    layout->setSpacing(12);

    /* 步骤编号徽标 */
    m_badge = new QLabel(this);
    m_badge->setObjectName(QStringLiteral("StepBadge"));
    m_badge->setAlignment(Qt::AlignCenter);
    m_badge->setFixedSize(34, 34);

    /* 文本区域：标题 + 详细指引 */
    auto *textBox = new QVBoxLayout;
    textBox->setSpacing(4);
    m_title = new QLabel(this);
    m_title->setObjectName(QStringLiteral("CardTitle"));
    m_instruction = new QLabel(this);
    m_instruction->setObjectName(QStringLiteral("BodyText"));
    m_instruction->setWordWrap(true);
    textBox->addWidget(m_title);
    textBox->addWidget(m_instruction);

    layout->addWidget(m_badge);
    layout->addLayout(textBox, 1);
}

/*
 * 设置步骤数据
 * 更新编号徽标、标题和指引文字
 */
void RouteStepItem::setStep(const RouteStep &step)
{
    m_step = step;
    m_badge->setText(QString::number(step.index));
    m_title->setText(step.title);
    m_instruction->setText(step.instruction);
}

/*
 * 设置选中状态
 * 通过属性驱动样式表刷新，实现高亮效果
 */
void RouteStepItem::setSelected(bool selected)
{
    setProperty("selected", selected);
    style()->unpolish(this);
    style()->polish(this);
    update();
}

/*
 * 获取当前步骤索引编号
 */
int RouteStepItem::stepIndex() const
{
    return m_step.index;
}

/*
 * 自绘事件
 * 绘制左侧连接线和选中状态下的高亮竖条
 */
void RouteStepItem::paintEvent(QPaintEvent *event)
{
    QFrame::paintEvent(event);

    QPainter painter(this);
    painter.setRenderHint(QPainter::Antialiasing);
    const bool selected = property("selected").toBool();
    const QColor line = selected ? QColor("#1E73F8") : QColor("#C7D7EA");
    painter.setPen(QPen(line, selected ? 4 : 2, Qt::SolidLine, Qt::RoundCap));
    painter.drawLine(QPointF(18, 0), QPointF(18, height()));
    if (selected) {
        painter.setPen(Qt::NoPen);
        painter.setBrush(QColor("#1E73F8"));
        painter.drawRoundedRect(QRectF(0, 10, 5, height() - 20), 3, 3);
    }
}

/*
 * 鼠标按下事件
 * 点击时发射 clicked 信号，传递当前步骤索引
 */
void RouteStepItem::mousePressEvent(QMouseEvent *event)
{
    if (event->button() == Qt::LeftButton) {
        emit clicked(m_step.index);
    }
    QFrame::mousePressEvent(event);
}
