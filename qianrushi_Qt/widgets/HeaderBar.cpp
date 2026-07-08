/*
 * 文件：HeaderBar.cpp
 * 说明：顶部导航栏控件的实现
 */

#include "HeaderBar.h"

#include <QHBoxLayout>
#include <QLabel>
#include <QPushButton>
#include <QResizeEvent>
#include <QStyle>
#include <QVariant>
#include <QVBoxLayout>

HeaderBar::HeaderBar(QWidget *parent)
    : QFrame(parent)
{
    setObjectName(QStringLiteral("HeaderBar"));
    setSizePolicy(QSizePolicy::Expanding, QSizePolicy::Fixed);

    auto *layout = new QHBoxLayout(this);
    layout->setContentsMargins(18, 14, 18, 12);
    layout->setSpacing(12);

    auto *titleBox = new QVBoxLayout;
    titleBox->setSpacing(2);
    auto *title = new QLabel(QStringLiteral("智能环境监测终端"), this);
    title->setObjectName(QStringLiteral("MainTitle"));
    auto *subtitle = new QLabel(QStringLiteral("为您提供便捷的出行指引"), this);
    subtitle->setObjectName(QStringLiteral("SubTitle"));
    titleBox->addWidget(title);
    titleBox->addWidget(subtitle);

    layout->addLayout(titleBox, 1);

    m_chatButton = makeNavButton(QStringLiteral("AI 问路"), QStringLiteral("chat"));
    m_mapButton = makeNavButton(QStringLiteral("地图"), QStringLiteral("map"));
    m_envButton = makeNavButton(QStringLiteral("环境监测"), QStringLiteral("env"));
    m_callButton = makeNavButton(QStringLiteral("通话"), QStringLiteral("call"));

    layout->addWidget(m_chatButton);
    layout->addWidget(m_mapButton);
    layout->addWidget(m_envButton);
    layout->addWidget(m_callButton);

    connect(m_chatButton, &QPushButton::clicked, this, &HeaderBar::chatHomeRequested);
    connect(m_mapButton, &QPushButton::clicked, this, &HeaderBar::mapRequested);
    connect(m_envButton, &QPushButton::clicked, this, &HeaderBar::envMonitorRequested);
    connect(m_callButton, &QPushButton::clicked, this, &HeaderBar::callPageRequested);
    updateNavTexts();
}

void HeaderBar::setActivePage(const QString &pageId)
{
    for (QPushButton *button : m_navButtons) {
        button->setProperty("active", button->property("pageId").toString() == pageId);
        button->style()->unpolish(button);
        button->style()->polish(button);
        button->update();
    }
}

void HeaderBar::setCallInProgress(bool active)
{
    if (m_callInProgress == active) {
        return;
    }
    m_callInProgress = active;
    updateNavTexts();
}

void HeaderBar::resizeEvent(QResizeEvent *event)
{
    QFrame::resizeEvent(event);
    const bool compact = width() < 900;
    if (m_compact != compact) {
        m_compact = compact;
        updateNavTexts();
    }
}

QPushButton *HeaderBar::makeNavButton(const QString &text, const QString &pageId)
{
    auto *button = new QPushButton(text, this);
    button->setObjectName(QStringLiteral("NavButton"));
    button->setProperty("pageId", pageId);
    button->setMinimumHeight(38);
    button->setCursor(Qt::PointingHandCursor);
    m_navButtons.append(button);
    return button;
}

void HeaderBar::updateNavTexts()
{
    if (m_chatButton) {
        m_chatButton->setText(m_compact ? QStringLiteral("AI") : QStringLiteral("AI 问路"));
    }
    if (m_mapButton) {
        m_mapButton->setText(QStringLiteral("地图"));
    }
    if (m_envButton) {
        m_envButton->setText(m_compact ? QStringLiteral("环境") : QStringLiteral("环境监测"));
    }
    if (m_callButton) {
        m_callButton->setText(m_callInProgress ? QStringLiteral("通话 ●") : QStringLiteral("通话"));
    }
}
