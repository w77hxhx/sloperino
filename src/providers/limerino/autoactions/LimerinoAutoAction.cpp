// SPDX-License-Identifier: MIT

#include "providers/limerino/autoactions/LimerinoAutoAction.hpp"

#include "util/serialize/List.hpp"  // QList<QString> inside Deserialize<LimerinoAutoAction>

#include <optional>

namespace chatterino::limerino {

bool LimerinoAutoAction::operator==(const LimerinoAutoAction &other) const
{
    return this->id == other.id && this->name == other.name &&
           this->enabled == other.enabled &&
           this->content.pattern == other.content.pattern &&
           this->content.caseSensitive == other.content.caseSensitive &&
           this->content.isRegex == other.content.isRegex &&
           this->sender.pattern == other.sender.pattern &&
           this->sender.caseSensitive == other.sender.caseSensitive &&
           this->sender.isRegex == other.sender.isRegex &&
           this->scope == other.scope && this->channels == other.channels &&
           this->action == other.action &&
           this->cooldownSeconds == other.cooldownSeconds;
}

bool LimerinoAutoAction::matchesChannel(const QString &channelKey) const
{
    const auto key = channelKey.toLower();
    switch (this->scope)
    {
        case Scope::AllExcept:
            return !this->channelSet.contains(key);
        case Scope::Only:
            return this->channelSet.contains(key);
    }
    return false;
}

void LimerinoAutoAction::normalize()
{
    this->channelSet.clear();
    QStringList channels;
    for (const auto &raw : this->channels)
    {
        const auto n = raw.trimmed().toLower();
        if (!n.isEmpty() && !this->channelSet.contains(n))
        {
            this->channelSet.insert(n);
            channels.append(n);
        }
    }
    this->channels = channels;

    if (this->cooldownSeconds < 0)
    {
        this->cooldownSeconds = 0;
    }

    // Matchers need their compiled regex refreshed after serde or mutation.
    this->content.normalize();
    this->sender.normalize();
}

}  // namespace chatterino::limerino

namespace pajlada {

namespace {

QString scopeToString(chatterino::limerino::LimerinoAutoAction::Scope scope)
{
    return scope == chatterino::limerino::LimerinoAutoAction::Scope::Only
               ? QStringLiteral("only")
               : QStringLiteral("allExcept");
}

chatterino::limerino::LimerinoAutoAction::Scope scopeFromString(
    const QString &s)
{
    return s == QStringLiteral("only")
               ? chatterino::limerino::LimerinoAutoAction::Scope::Only
               : chatterino::limerino::LimerinoAutoAction::Scope::AllExcept;
}

}  // namespace

rapidjson::Value Serialize<chatterino::limerino::LimerinoAutoAction>::get(
    const chatterino::limerino::LimerinoAutoAction &value,
    rapidjson::Document::AllocatorType &a)
{
    rapidjson::Value ret(rapidjson::kObjectType);

    chatterino::rj::set(ret, "id",
                        value.id.toString(QUuid::WithoutBraces), a);
    chatterino::rj::set(ret, "name", value.name, a);
    chatterino::rj::set(ret, "enabled", value.enabled, a);
    chatterino::rj::set(ret, "content", value.content, a);
    chatterino::rj::set(ret, "sender", value.sender, a);
    chatterino::rj::set(ret, "scope", scopeToString(value.scope), a);

    // QStringList serialisation: mirror the manual pattern from
    // HighlightGroup.cpp - pajlada's QList serializer trips clang-cl on
    // the QByteArray temporary it generates per QString.
    {
        rapidjson::Value channelsArr(rapidjson::kArrayType);
        for (const auto &channel : value.channels)
        {
            rapidjson::Value v;
            const QByteArray utf8 = channel.toUtf8();
            v.SetString(utf8.constData(),
                        static_cast<rapidjson::SizeType>(utf8.size()), a);
            channelsArr.PushBack(v, a);
        }
        ret.AddMember("channels", channelsArr, a);
    }

    chatterino::rj::set(ret, "action", value.action, a);
    chatterino::rj::set(ret, "cooldownSeconds", value.cooldownSeconds, a);

    return ret;
}

chatterino::limerino::LimerinoAutoAction
    Deserialize<chatterino::limerino::LimerinoAutoAction>::get(
        const rapidjson::Value &value, bool *error)
{
    chatterino::limerino::LimerinoAutoAction a;
    if (!value.IsObject())
    {
        PAJLADA_REPORT_ERROR(error)
        return a;
    }

    QString idStr;
    QString scopeStr;
    int cooldown = a.cooldownSeconds;

    chatterino::rj::getSafe(value, "id", idStr);
    chatterino::rj::getSafe(value, "name", a.name);
    chatterino::rj::getSafe(value, "enabled", a.enabled);
    chatterino::rj::getSafe(value, "content", a.content);
    chatterino::rj::getSafe(value, "sender", a.sender);
    chatterino::rj::getSafe(value, "scope", scopeStr);
    chatterino::rj::getSafe(value, "channels", a.channels);
    chatterino::rj::getSafe(value, "action", a.action);
    chatterino::rj::getSafe(value, "cooldownSeconds", cooldown);

    const auto parsed = QUuid::fromString(idStr);
    if (!parsed.isNull())
    {
        a.id = parsed;
    }
    a.scope = scopeFromString(scopeStr);
    if (cooldown >= 0)
    {
        a.cooldownSeconds = cooldown;
    }
    a.normalize();

    return a;
}

}  // namespace pajlada
