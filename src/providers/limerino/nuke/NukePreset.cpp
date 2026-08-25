// SPDX-License-Identifier: MIT

#include "providers/limerino/nuke/NukePreset.hpp"

#include <QStringBuilder>

namespace chatterino::limerino {

bool LimerinoNukePreset::operator==(const LimerinoNukePreset &other) const
{
    return this->name == other.name &&
           this->content.pattern == other.content.pattern &&
           this->content.caseSensitive == other.content.caseSensitive &&
           this->content.isRegex == other.content.isRegex &&
           this->sender.pattern == other.sender.pattern &&
           this->sender.caseSensitive == other.sender.caseSensitive &&
           this->sender.isRegex == other.sender.isRegex &&
           this->lookbackSeconds == other.lookbackSeconds &&
           this->action == other.action &&
           this->timeoutSeconds == other.timeoutSeconds &&
           this->reason == other.reason;
}

}  // namespace chatterino::limerino

namespace pajlada {

namespace {

QString actionToString(chatterino::limerino::NukeAction action)
{
    using chatterino::limerino::NukeAction;
    switch (action)
    {
        case NukeAction::Ban:
            return QStringLiteral("ban");
        case NukeAction::Timeout:
            return QStringLiteral("timeout");
        case NukeAction::Warn:
            return QStringLiteral("warn");
        case NukeAction::Delete:
            return QStringLiteral("delete");
        case NukeAction::DeleteAndTimeout:
            return QStringLiteral("deleteAndTimeout");
    }
    return QStringLiteral("ban");
}

chatterino::limerino::NukeAction actionFromString(const QString &s)
{
    using chatterino::limerino::NukeAction;
    if (s == QStringLiteral("timeout"))
    {
        return NukeAction::Timeout;
    }
    if (s == QStringLiteral("warn"))
    {
        return NukeAction::Warn;
    }
    if (s == QStringLiteral("delete"))
    {
        return NukeAction::Delete;
    }
    if (s == QStringLiteral("deleteAndTimeout"))
    {
        return NukeAction::DeleteAndTimeout;
    }
    return NukeAction::Ban;
}

}  // namespace

rapidjson::Value Serialize<chatterino::limerino::LimerinoNukePreset>::get(
    const chatterino::limerino::LimerinoNukePreset &value,
    rapidjson::Document::AllocatorType &a)
{
    rapidjson::Value ret(rapidjson::kObjectType);

    chatterino::rj::set(ret, "name", value.name, a);
    chatterino::rj::set(ret, "content", value.content, a);
    chatterino::rj::set(ret, "sender", value.sender, a);
    chatterino::rj::set(ret, "lookback", value.lookbackSeconds, a);
    chatterino::rj::set(ret, "action", actionToString(value.action), a);
    chatterino::rj::set(ret, "timeoutSeconds", value.timeoutSeconds, a);
    chatterino::rj::set(ret, "reason", value.reason, a);

    return ret;
}

chatterino::limerino::LimerinoNukePreset
    Deserialize<chatterino::limerino::LimerinoNukePreset>::get(
        const rapidjson::Value &value, bool *error)
{
    chatterino::limerino::LimerinoNukePreset p;
    if (!value.IsObject())
    {
        PAJLADA_REPORT_ERROR(error)
        return p;
    }

    QString actionStr;
    chatterino::rj::getSafe(value, "name", p.name);
    chatterino::rj::getSafe(value, "content", p.content);
    chatterino::rj::getSafe(value, "sender", p.sender);
    chatterino::rj::getSafe(value, "lookback", p.lookbackSeconds);
    chatterino::rj::getSafe(value, "action", actionStr);
    chatterino::rj::getSafe(value, "timeoutSeconds", p.timeoutSeconds);
    chatterino::rj::getSafe(value, "reason", p.reason);

    p.action = actionFromString(actionStr);
    if (p.lookbackSeconds <= 0)
    {
        p.lookbackSeconds = 600;  // migration default
    }
    if (p.timeoutSeconds <= 0)
    {
        p.timeoutSeconds = 600;
    }

    return p;
}

}  // namespace pajlada
