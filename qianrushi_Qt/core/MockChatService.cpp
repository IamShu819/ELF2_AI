/*!
 * @file       MockChatService.cpp
 * @brief      模拟聊天服务实现
 */

#include "MockChatService.h"

#include "core/TerminalLocationService.h"

#include <QStringList>

namespace {
bool containsAny(const QString &text, const QStringList &keywords)
{
    for (const QString &keyword : keywords) {
        if (text.contains(keyword)) {
            return true;
        }
    }
    return false;
}

bool isCurrentLocationQuery(const QString &text)
{
    return containsAny(text, {
        QStringLiteral("我现在在哪"),
        QStringLiteral("我在哪"),
        QStringLiteral("当前位置"),
        QStringLiteral("我的位置"),
        QStringLiteral("现在在什么位置"),
        QStringLiteral("现在位置"),
        QStringLiteral("这里是哪里"),
        QStringLiteral("设备位置"),
        QStringLiteral("终端位置")
    });
}

bool isRouteOrPlaceQuery(const QString &text)
{
    const bool hasPlace = containsAny(text, {
        QStringLiteral("医院"),
        QStringLiteral("公交"),
        QStringLiteral("卫生间"),
        QStringLiteral("厕所"),
        QStringLiteral("政务"),
        QStringLiteral("办事"),
        QStringLiteral("社区"),
        QStringLiteral("公园"),
        QStringLiteral("银行"),
        QStringLiteral("餐厅"),
        QStringLiteral("吃饭"),
        QStringLiteral("肯德基")
    });
    const bool hasMapIntent = containsAny(text, {
        QStringLiteral("附近"),
        QStringLiteral("最近"),
        QStringLiteral("哪里"),
        QStringLiteral("在哪"),
        QStringLiteral("怎么走"),
        QStringLiteral("路线"),
        QStringLiteral("导航"),
        QStringLiteral("地图"),
        QStringLiteral("过去"),
        QStringLiteral("去")
    });
    return hasPlace && hasMapIntent;
}

bool isMapOnlyQuery(const QString &text)
{
    return containsAny(text, {
        QStringLiteral("查看地图"),
        QStringLiteral("打开地图"),
        QStringLiteral("显示地图"),
        QStringLiteral("看地图")
    });
}
}

/*!
 * 构造函数
 * @param dataService 数据服务实例
 * @param parent 父对象
 */
MockChatService::MockChatService(MockDataService *dataService, QObject *parent)
    : QObject(parent)
    , m_dataService(dataService)
{
}

/*!
 * 根据用户输入文本生成回复
 * @param text 用户输入
 * @return 包含兴趣点和回复文本的聊天回复结构
 */
ChatReply MockChatService::replyForText(const QString &text) const
{
    ChatReply reply;
    reply.userText = text;

    if (isCurrentLocationQuery(text) || isMapOnlyQuery(text)) {
        const TerminalLocation location = TerminalLocationService::cachedLocation();
        reply.usesMap = true;
        reply.showsRoute = false;
        reply.assistantText = QStringLiteral("您当前在%1附近。\n右侧已显示设备当前位置。")
            .arg(location.name);
        return reply;
    }

    if (!isRouteOrPlaceQuery(text)) {
        reply.usesMap = false;
        reply.showsRoute = false;
        return reply;
    }

    const POIInfo poi = m_dataService->poiByKeyword(text);
    reply.usesMap = true;
    reply.showsRoute = true;
    reply.poi = poi;
    reply.assistantText = QStringLiteral("好的，为您找到最近的【%1】。\n距离约 %2 米，预计步行 %3 分钟。\n%4\n右侧已显示本地地图和步行路线。")
        .arg(poi.name)
        .arg(poi.distanceMeters)
        .arg(poi.durationMinutes)
        .arg(poi.description);
    return reply;
}

/*!
 * 为指定兴趣点生成快速提问文本
 * @param poi 兴趣点
 * @return 快速提问文本
 */
QString MockChatService::quickQuestionForPOI(const POIInfo &poi) const
{
    if (poi.type == POIType::Toilet) {
        return QStringLiteral("附近的卫生间在哪里？");
    }
    if (poi.type == POIType::Hospital) {
        return QStringLiteral("附近的医院怎么走？");
    }
    if (poi.type == POIType::BusStation) {
        return QStringLiteral("最近的公交站在哪里？");
    }
    if (poi.type == POIType::Government) {
        return QStringLiteral("我要去政务服务中心");
    }
    return QStringLiteral("从这里去%1怎么走？").arg(poi.name);
}
