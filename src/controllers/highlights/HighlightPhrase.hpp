// SPDX-FileCopyrightText: 2018 Contributors to Chatterino <https://chatterino.com>
//
// SPDX-License-Identifier: MIT

#pragma once

#include "util/RapidjsonHelpers.hpp"
#include "util/RapidJsonSerializeQString.hpp"

#include <pajlada/serialize.hpp>
#include <QColor>
#include <QRegularExpression>
#include <QString>
#include <QUrl>
#include <QUuid>

#include <memory>

namespace chatterino {

class HighlightPhrase
{
public:
    bool operator==(const HighlightPhrase &other) const;

    HighlightPhrase(const QString &pattern, bool showInMentions, bool hasAlert,
                    bool hasSound, bool isRegex, bool isCaseSensitive,
                    const QString &soundUrl, QColor color,
                    const QUuid &groupId = QUuid());

    HighlightPhrase(const QString &pattern, bool showInMentions, bool hasAlert,
                    bool hasSound, bool isRegex, bool isCaseSensitive,
                    const QString &soundUrl, std::shared_ptr<QColor> color,
                    const QUuid &groupId = QUuid());

    const QString &getPattern() const;
    bool showInMentions() const;
    bool hasAlert() const;

    /// Which highlight group this phrase belongs to.
    /// A null/default QUuid means the Default group (i.e. global).
    const QUuid &groupId() const;

    bool hasSound() const;

    bool hasCustomSound() const;

    bool isRegex() const;
    bool isValid() const;
    bool isMatch(const QString &subject) const;
    bool isCaseSensitive() const;
    const QUrl &getSoundUrl() const;
    const std::shared_ptr<QColor> getColor() const;

    static QColor FALLBACK_HIGHLIGHT_COLOR;

    static QColor FALLBACK_SELF_MESSAGE_HIGHLIGHT_COLOR;
    static QColor FALLBACK_REDEEMED_HIGHLIGHT_COLOR;
    static QColor FALLBACK_SUB_COLOR;
    static QColor FALLBACK_FOLLOW_COLOR;
    static QColor FALLBACK_WATCH_STREAK_COLOR;
    static QColor FALLBACK_FIRST_MESSAGE_HIGHLIGHT_COLOR;
    static QColor FALLBACK_ELEVATED_MESSAGE_HIGHLIGHT_COLOR;
    static QColor FALLBACK_THREAD_HIGHLIGHT_COLOR;
    static QColor FALLBACK_AUTOMOD_HIGHLIGHT_COLOR;
    static QColor FALLBACK_ANNOUNCEMENT_HIGHLIGHT_COLOR;
    static QColor ANNOUNCEMENT_BLUE_HIGHLIGHT_COLOR;
    static QColor ANNOUNCEMENT_GREEN_HIGHLIGHT_COLOR;
    static QColor ANNOUNCEMENT_ORANGE_HIGHLIGHT_COLOR;
    static QColor ANNOUNCEMENT_PURPLE_HIGHLIGHT_COLOR;

private:
    QString pattern_;
    bool showInMentions_;
    bool hasAlert_;
    bool hasSound_;
    bool isRegex_;
    bool isCaseSensitive_;
    QUrl soundUrl_;
    std::shared_ptr<QColor> color_;
    QRegularExpression regex_;

    /// Limerino: per-channel highlight group membership.
    /// Null/default = Default group = global behaviour.
    QUuid groupId_;
};

}  // namespace chatterino

namespace pajlada {

namespace {
chatterino::HighlightPhrase constructError()
{
    return chatterino::HighlightPhrase(QString(), false, false, false, false,
                                       false, QString(), QColor());
}
}  // namespace

template <>
struct Serialize<chatterino::HighlightPhrase> {
    static rapidjson::Value get(const chatterino::HighlightPhrase &value,
                                rapidjson::Document::AllocatorType &a)
    {
        rapidjson::Value ret(rapidjson::kObjectType);

        chatterino::rj::set(ret, "pattern", value.getPattern(), a);
        chatterino::rj::set(ret, "showInMentions", value.showInMentions(), a);
        chatterino::rj::set(ret, "alert", value.hasAlert(), a);
        chatterino::rj::set(ret, "sound", value.hasSound(), a);
        chatterino::rj::set(ret, "regex", value.isRegex(), a);
        chatterino::rj::set(ret, "case", value.isCaseSensitive(), a);
        chatterino::rj::set(ret, "soundUrl", value.getSoundUrl().toString(), a);
        chatterino::rj::set(ret, "color",
                            value.getColor()->name(QColor::HexArgb), a);

        if (!value.groupId().isNull())
        {
            chatterino::rj::set(ret, "groupId",
                                value.groupId().toString(QUuid::WithoutBraces),
                                a);
        }

        return ret;
    }
};

template <>
struct Deserialize<chatterino::HighlightPhrase> {
    static chatterino::HighlightPhrase get(const rapidjson::Value &value,
                                           bool *error = nullptr)
    {
        if (!value.IsObject())
        {
            PAJLADA_REPORT_ERROR(error)

            return constructError();
        }

        QString _pattern;
        bool _showInMentions = true;
        bool _hasAlert = true;
        bool _hasSound = false;
        bool _isRegex = false;
        bool _isCaseSensitive = false;
        QString _soundUrl;
        QString encodedColor;
        QString groupIdStr;

        chatterino::rj::getSafe(value, "pattern", _pattern);
        chatterino::rj::getSafe(value, "showInMentions", _showInMentions);
        chatterino::rj::getSafe(value, "alert", _hasAlert);
        chatterino::rj::getSafe(value, "sound", _hasSound);
        chatterino::rj::getSafe(value, "regex", _isRegex);
        chatterino::rj::getSafe(value, "case", _isCaseSensitive);
        chatterino::rj::getSafe(value, "soundUrl", _soundUrl);
        chatterino::rj::getSafe(value, "color", encodedColor);
        chatterino::rj::getSafe(value, "groupId", groupIdStr);

        auto _color = QColor(encodedColor);
        if (!_color.isValid())
        {
            _color = chatterino::HighlightPhrase::FALLBACK_HIGHLIGHT_COLOR;
        }

        // Limerino: absent or unparseable groupId leaves a null QUuid,
        // which the resolver treats as "ungrouped -> applies in every
        // channel".
        auto _groupId = QUuid::fromString(groupIdStr);

        return chatterino::HighlightPhrase(
            _pattern, _showInMentions, _hasAlert, _hasSound, _isRegex,
            _isCaseSensitive, _soundUrl, _color, _groupId);
    }
};

}  // namespace pajlada
