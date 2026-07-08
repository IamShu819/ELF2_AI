/*
 * 文件：VoiceButton.h
 * 说明：语音交互按钮控件，支持空闲、聆听、处理三种状态动画
 */

#pragma once

#include <QPushButton>

class QVariantAnimation;

/* 语音按钮状态枚举 */
enum class VoiceState {
    Idle,       /* 空闲状态 */
    Listening,  /* 聆听中 */
    Processing  /* 处理中 */
};

/*
 * 语音交互按钮控件
 * 提供脉冲动画和状态切换，通过 QPainter 自绘圆形麦克风图标
 */
class VoiceButton : public QPushButton
{
    Q_OBJECT
    /* 脉冲动画值属性，驱动波纹扩散效果 */
    Q_PROPERTY(qreal pulse READ pulse WRITE setPulse)

public:
    explicit VoiceButton(QWidget *parent = nullptr);

    /* 获取当前语音状态 */
    VoiceState state() const;
    /* 设置语音状态，切换对应的显示文本和动画 */
    void setState(VoiceState state);
    /* 获取当前脉冲值 */
    qreal pulse() const;
    /* 设置脉冲值，触发重绘更新波纹效果 */
    void setPulse(qreal value);

protected:
    /* 自绘事件：绘制麦克风按钮及波纹扩散效果 */
    void paintEvent(QPaintEvent *event) override;

private:
    VoiceState m_state = VoiceState::Idle;       /* 当前语音状态 */
    qreal m_pulse = 0.0;                         /* 当前脉冲数值（0.0 ~ 1.0） */
    QVariantAnimation *m_animation = nullptr;    /* 脉冲循环动画 */
};
