// SPDX-License-Identifier: MIT
// Per-channel highlight groups (Limerino-specific, no upstream equivalent).
//
// A highlight group owns a scope (AllExcept or Only) and a channel list.
// All pre-existing highlights migrate into the undeletable DEFAULT group,
// which is born as AllExcept with an empty list (applies everywhere).

#pragma once

#include "util/RapidjsonHelpers.hpp"
#include "util/RapidJsonSerializeQString.hpp"

#include <pajlada/serialize.hpp>
#include <QSet>
#include <QString>
#include <QStringList>
#include <QUuid>

namespace chatterino {

class HighlightGroup
{
public:
    enum class Scope {
        AllExcept,  // applies everywhere except `channels`; empty == everywhere
        Only,       // applies only in `channels`
    };

    HighlightGroup(QUuid id, QString name, Scope scope, QStringList channels);

    bool operator==(const HighlightGroup &other) const;

    const QUuid &id() const;
    const QString &name() const;
    Scope scope() const;
    const QStringList &channels() const;

    bool isDefault() const;  // id() == DEFAULT_ID
    bool matches(const QString &channelKey) const;
    QString displayName() const;  // name, or scope summary if empty

    static const QUuid DEFAULT_ID;  // fixed, well-known constant

private:
    QUuid id_;
    QString name_;
    Scope scope_;
    QStringList channels_;        // normalised, see .cpp
    QSet<QString> channelSet_;    // derived at construction, for matches()
};

}  // namespace chatterino

namespace pajlada {

template <>
struct Serialize<chatterino::HighlightGroup> {
    static rapidjson::Value get(const chatterino::HighlightGroup &value,
                                rapidjson::Document::AllocatorType &a);
};

template <>
struct Deserialize<chatterino::HighlightGroup> {
    static chatterino::HighlightGroup get(const rapidjson::Value &value,
                                          bool *error = nullptr);
};

}  // namespace pajlada
