/*
 * 文件：VoiceButton.cpp
 * 说明：语音交互按钮控件的实现
 */

#include "VoiceButton.h"

#include <QLinearGradient>
#include <QPainter>
#include <QRadialGradient>
#include <QVariantAnimation>

/*
 * 构造函数：初始化按钮尺寸、鼠标样式及脉冲动画
 */
VoiceButton::VoiceButton(QWidget *parent)
    : QPushButton(parent)
{
    setCursor(Qt::PointingHandCursor);
    setFixedSize(158, 158);
    setText(QString());

    /* 创建循环脉冲动画 */
    m_animation = new QVariantAnimation(this);
    m_animation->setStartValue(0.0);
    m_animation->setEndValue(1.0);
    m_animation->setDuration(1600);
    m_animation->setLoopCount(-1);
    connect(m_animation, &QVariantAnimation::valueChanged, this, [this](const QVariant &value) {
        setPulse(value.toReal());
    });
    m_animation->start();
}

/*
 * 获取当前语音状态
 */
VoiceState VoiceButton::state() const
{
    return m_state;
}

/*
 * 设置语音状态
 * 根据状态切换显示文本，并确保动画处于运行状态
 */
void VoiceButton::setState(VoiceState state)
{
    m_state = state;
    if (m_state == VoiceState::Idle) {
        setText(QString());
        if (m_animation->state() != QAbstractAnimation::Running) {
            m_animation->start();
        }
    } else if (m_state == VoiceState::Listening) {
        setText(QStringLiteral("正在\n聆听"));
        m_animation->start();
    } else {
        setText(QStringLiteral("正在\n处理"));
        m_animation->start();
    }
    update();
}

/*
 * 获取当前脉冲值
 */
qreal VoiceButton::pulse() const
{
    return m_pulse;
}

/*
 * 设置脉冲值
 * 赋值后触发重绘，驱动波纹扩散动画效果
 */
void VoiceButton::setPulse(qreal value)
{
    m_pulse = value;
    update();
}

/*
 * 自绘事件
 * 绘制圆形麦克风按钮，包含波纹扩散、渐变底色及麦克风图标
 */
void VoiceButton::paintEvent(QPaintEvent *)
{
    QPainter painter(this);
    painter.setRenderHint(QPainter::Antialiasing);

    const QPoint center = rect().center();
    const int available = qMin(width(), height());
    const int buttonDiameter = qMin(110, qMax(56, available - 48));
    const int radius = buttonDiameter / 2;

    /* 绘制脉冲波纹 */
    const int waveRadius = radius + int(20 * m_pulse);
    QColor wave(59, 130, 246, int((m_state == VoiceState::Idle ? 44 : 92) * (1.0 - m_pulse)));
    painter.setPen(Qt::NoPen);
    painter.setBrush(wave);
    painter.drawEllipse(center, waveRadius, waveRadius);

    /* 绘制底部柔光阴影 */
    QRadialGradient softShadow(QPointF(center.x(), center.y() + 15), radius + 26);
    softShadow.setColorAt(0.0, QColor(37, 99, 235, isDown() ? 74 : 58));
    softShadow.setColorAt(0.55, QColor(37, 99, 235, isDown() ? 36 : 28));
    softShadow.setColorAt(1.0, QColor(37, 99, 235, 0));
    painter.setBrush(softShadow);
    painter.drawEllipse(QPoint(center.x(), center.y() + 15), radius + 26, radius + 20);

    /* 绘制内凹阴影 */
    painter.setBrush(QColor(15, 23, 42, 22));
    painter.drawEllipse(QPoint(center.x(), center.y() + 7), radius - 3, radius - 3);

    /* 绘制按钮渐变底色 */
    QLinearGradient fill(center.x() - radius, center.y() - radius, center.x() + radius, center.y() + radius);
    fill.setColorAt(0.0, isDown() ? QColor("#2563EB") : QColor("#3B82F6"));
    fill.setColorAt(1.0, isDown() ? QColor("#1D4ED8") : QColor("#2563EB"));
    painter.setPen(Qt::NoPen);
    painter.setBrush(fill);
    painter.drawEllipse(center, radius, radius);

    /* 绘制麦克风图标 */
    const qreal scale = buttonDiameter / 110.0;
    painter.setPen(QPen(Qt::white, 4.2 * scale, Qt::SolidLine, Qt::RoundCap, Qt::RoundJoin));
    const QRectF mic(center.x() - 10 * scale, center.y() - 30 * scale, 20 * scale, 34 * scale);
    painter.drawRoundedRect(mic, 10 * scale, 10 * scale);
    painter.drawLine(QPointF(center.x(), center.y() + 4 * scale), QPointF(center.x(), center.y() + 25 * scale));
    painter.drawArc(QRectF(center.x() - 25 * scale, center.y() - 16 * scale, 50 * scale, 50 * scale), 210 * 16, 120 * 16);
    painter.drawLine(QPointF(center.x() - 16 * scale, center.y() + 28 * scale), QPointF(center.x() + 16 * scale, center.y() + 28 * scale));
}
