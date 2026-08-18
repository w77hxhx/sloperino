// SPDX-FileCopyrightText: 2026 Contributors to Chatterino <https://chatterino.com>
//
// SPDX-License-Identifier: MIT

#pragma once

#include "messages/Emote.hpp"
#include "util/RapidjsonHelpers.hpp"
#include "util/RapidJsonSerializeQString.hpp"

#include <pajlada/serialize.hpp>
#include <QString>

namespace chatterino {

class EmoteAlias
{
public:
    EmoteAlias(const QString &word = {}, const QString &link = {},
               bool isCaseSensitive = false);

    [[nodiscard]] const QString &word() const;
    [[nodiscard]] const QString &link() const;
    [[nodiscard]] bool isCaseSensitive() const;

    [[nodiscard]] bool matches(const QString &candidate) const;
    [[nodiscard]] EmotePtr createEmote() const;

private:
    QString word_;
    QString link_;
    bool isCaseSensitive_{false};
};

}  // namespace chatterino

namespace pajlada {

template <>
struct Serialize<chatterino::EmoteAlias> {
    static rapidjson::Value get(const chatterino::EmoteAlias &value,
                                rapidjson::Document::AllocatorType &a)
    {
        rapidjson::Value ret(rapidjson::kObjectType);
        chatterino::rj::set(ret, "word", value.word(), a);
        chatterino::rj::set(ret, "link", value.link(), a);
        chatterino::rj::set(ret, "isCaseSensitive", value.isCaseSensitive(), a);
        return ret;
    }
};

template <>
struct Deserialize<chatterino::EmoteAlias> {
    static chatterino::EmoteAlias get(const rapidjson::Value &value,
                                      bool *error = nullptr)
    {
        if (!value.IsObject())
        {
            PAJLADA_REPORT_ERROR(error)
            return chatterino::EmoteAlias();
        }

        QString _word;
        QString _link;
        bool _isCaseSensitive = false;

        chatterino::rj::getSafe(value, "word", _word);
        chatterino::rj::getSafe(value, "link", _link);
        chatterino::rj::getSafe(value, "isCaseSensitive", _isCaseSensitive);

        return chatterino::EmoteAlias(_word, _link, _isCaseSensitive);
    }
};

}  // namespace pajlada
