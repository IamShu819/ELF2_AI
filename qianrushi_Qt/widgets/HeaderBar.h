/*
 * 文件：HeaderBar.h
 * 说明：顶部导航栏控件，提供页面标题、副标题及导航入口
 */

#pragma once

#include <QFrame>
#include <QList>

class QPushButton;
class QResizeEvent;

class HeaderBar : public QFrame
{
    Q_OBJECT

public:
    explicit HeaderBar(QWidget *parent = nullptr);
    void setActivePage(const QString &pageId);

signals:
    void chatHomeRequested();
    void mapRequested();
    void envMonitorRequested();
    void callPageRequested();

public slots:
    void setCallInProgress(bool active);

protected:
    void resizeEvent(QResizeEvent *event) override;

private:
    QPushButton *makeNavButton(const QString &text, const QString &pageId);
    void updateNavTexts();

    QList<QPushButton *> m_navButtons;
    QPushButton *m_chatButton = nullptr;
    QPushButton *m_mapButton = nullptr;
    QPushButton *m_envButton = nullptr;
    QPushButton *m_callButton = nullptr;
    bool m_compact = false;
    bool m_callInProgress = false;
};
