// SPDX-License-Identifier: MIT

#include "providers/limerino/highlights/HighlightGroup.hpp"

#include "util/serialize/List.hpp"  // pajlada::Serialize<QList<T>>

#include <QStringBuilder>

namespace chatterino {

// Well-known Default group ID. NOT the null UUID: null (`QUuid{}`) is
// used by HighlightController to mark *never-grouped, always-global* checks,
// so Default must be a real, distinct, constant identifier.
const QUuid HighlightGroup::DEFAULT_ID =
    QUuid::fromString(QStringLiteral("ffffffff-ffff-ffff-ffff-ffffffffffff"));

HighlightGroup::HighlightGroup(QUuid id, QString name, Scope scope,
                               QStringList channels)
    : id_(std::move(id))
    , name_(std::move(name))
    , scope_(scope)
{
    // Normalise: trim, lowercase, drop empties, dedupe.
    for (const auto &raw : channels)
    {
        auto normalised = raw.trimmed().toLower();
        if (!normalised.isEmpty() && !this->channelSet_.contains(normalised))
        {
            this->channelSet_.insert(normalised);
            this->channels_.append(normalised);
        }
    }
}

bool HighlightGroup::operator==(const HighlightGroup &other) const
{
    return std::tie(this->id_, this->name_, this->scope_, this->channels_) ==
           std::tie(other.id_, other.name_, other.scope_, other.channels_);
}

const QUuid &HighlightGroup::id() const
{
    return this->id_;
}

const QString &HighlightGroup::name() const
{
    return this->name_;
}

HighlightGroup::Scope HighlightGroup::scope() const
{
    return this->scope_;
}

const QStringList &HighlightGroup::channels() const
{
    return this->channels_;
}

bool HighlightGroup::isDefault() const
{
    return this->id_ == DEFAULT_ID;
}

bool HighlightGroup::matches(const QString &channelKey) const
{
    const auto key = channelKey.toLower();
    switch (this->scope_)
    {
        case Scope::AllExcept:
            return !this->channelSet_.contains(key);
        case Scope::Only:
            return this->channelSet_.contains(key);
    }
    return false;  // unreachable
}

QString HighlightGroup::displayName() const
{
    if (!this->name_.isEmpty())
    {
        return this->name_;
    }
    const auto joined = this->channels_.join(QStringLiteral(", "));
    switch (this->scope_)
    {
        case Scope::AllExcept:
            if (this->channels_.isEmpty())
            {
                return QStringLiteral("Everywhere");
            }
            return QStringLiteral("All except: ") % joined;
        case Scope::Only:
            return QStringLiteral("Only: ") % joined;
    }
    return {};  // unreachable
}

}  // namespace chatterino

namespace pajlada {

rapidjson::Value Serialize<chatterino::HighlightGroup>::get(
    const chatterino::HighlightGroup &value,
    rapidjson::Document::AllocatorType &a)
{
    rapidjson::Value ret(rapidjson::kObjectType);

    chatterino::rj::set(
        ret, "id", value.id().toString(QUuid::WithoutBraces), a);
    chatterino::rj::set(ret, "name", value.name(), a);

    QString scopeStr;
    switch (value.scope())
    {
        case chatterino::HighlightGroup::Scope::AllExcept:
            scopeStr = QStringLiteral("allExcept");
            break;
        case chatterino::HighlightGroup::Scope::Only:
            scopeStr = QStringLiteral("only");
            break;
    }
    chatterino::rj::set(ret, "scope", scopeStr, a);

    // Channels: write manually as a JSON array to avoid relying on the QList
    // serializer, which trips clang-cl through the QByteArray temporary
    // produced by pajlada::Serialize<QString>.
    rapidjson::Value channelsArr(rapidjson::kArrayType);
    for (const auto &channel : value.channels())
    {
        rapidjson::Value v;
        QByteArray utf8 = channel.toUtf8();
        v.SetString(utf8.constData(), static_cast<rapidjson::SizeType>(
                                         utf8.size()),
                    a);
        channelsArr.PushBack(v, a);
    }
    ret.AddMember("channels", channelsArr, a);

    return ret;
}

chatterino::HighlightGroup Deserialize<chatterino::HighlightGroup>::get(
    const rapidjson::Value &value, bool *error)
{
    if (!value.IsObject())
    {
        PAJLADA_REPORT_ERROR(error)
        // Fall back to a Default-scoped group on parse failure.
        return {chatterino::HighlightGroup::DEFAULT_ID, QString(),
                chatterino::HighlightGroup::Scope::AllExcept, {}};
    }

    QString idStr;
    QString name;
    QString scopeStr;
    QStringList channels;

    chatterino::rj::getSafe(value, "id", idStr);
    chatterino::rj::getSafe(value, "name", name);
    chatterino::rj::getSafe(value, "scope", scopeStr);
    chatterino::rj::getSafe(value, "channels", channels);

    auto id = QUuid::fromString(idStr);
    // NOTE: a missing/unparseable id yields a null QUuid. For *groups* the
    // null id would be unusuable (the Default group has a distinct constant),
    // so remap null to DEFAULT_ID here. This only triggers for malformed
    // entries in settings.json, not for normal deserialization.
    if (id.isNull())
    {
        id = chatterino::HighlightGroup::DEFAULT_ID;
    }

    auto scope = chatterino::HighlightGroup::Scope::AllExcept;
    if (scopeStr == QStringLiteral("only"))
    {
        scope = chatterino::HighlightGroup::Scope::Only;
    }
    // anything else (including missing) stays AllExcept

    return {id, name, scope, channels};
}

}  // namespace pajlada
