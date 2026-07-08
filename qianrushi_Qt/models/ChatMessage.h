#pragma once

#include <QString>

// 聊天消息结构体，表示一次对话中的单条消息
struct ChatMessage {
    // 消息角色枚举
    enum Role {
        User,       // 用户消息
        Assistant   // AI助手回复消息
    };

    Role role = User;       // 消息发送角色
    QString text;           // 消息文本内容
    QString relatedPoiId;   // 关联的兴趣点ID（如有）
};