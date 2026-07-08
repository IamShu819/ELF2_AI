/*!
 * @file       MockChatService.h
 * @brief      模拟聊天服务，生成对话回复与快速提问
 */

#pragma once

#include <QObject>

#include "core/MockDataService.h"
#include "models/ChatMessage.h"

/*! 聊天回复结构，包含用户文本、助手回复及关联的兴趣点 */
struct ChatReply {
    QString userText;           /*!< 用户输入文本 */
    QString assistantText;      /*!< 助手回复文本 */
    POIInfo poi;                /*!< 关联的兴趣点 */
    bool usesMap = false;       /*!< 是否需要展示地图 */
    bool showsRoute = false;    /*!< 是否需要展示路线 */
};

/*! 模拟聊天服务，根据用户输入生成回复和快捷问题 */
class MockChatService : public QObject
{
    Q_OBJECT

public:
    /*!
     * @param dataService 数据服务实例，用于查询兴趣点
     * @param parent 父对象
     */
    explicit MockChatService(MockDataService *dataService, QObject *parent = nullptr);

    /*! 根据用户输入文本生成回复 */
    ChatReply replyForText(const QString &text) const;
    /*! 为指定兴趣点生成快速提问文本 */
    QString quickQuestionForPOI(const POIInfo &poi) const;

private:
    MockDataService *m_dataService = nullptr;   /*!< 数据服务指针 */
};
