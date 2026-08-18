// SPDX-FileCopyrightText: 2026 Contributors to Chatterino <https://chatterino.com>
//
// SPDX-License-Identifier: MIT

#include "controllers/aliases/EmoteAlias.hpp"

#include "messages/ImageSet.hpp"

#include <QUrl>

namespace chatterino {

EmoteAlias::EmoteAlias(const QString &word, const QString &link,
                       bool isCaseSensitive)
    : word_(word.trimmed())
    , link_(link.trimmed())
    , isCaseSensitive_(isCaseSensitive)
{
}

const QString &EmoteAlias::word() const
{
    return this->word_;
}

const QString &EmoteAlias::link() const
{
    return this->link_;
}

bool EmoteAlias::isCaseSensitive() const
{
    return this->isCaseSensitive_;
}

bool EmoteAlias::matches(const QString &candidate) const
{
    if (this->word_.isEmpty())
    {
        return false;
    }
    return this->word_.compare(candidate, this->isCaseSensitive_
                                              ? Qt::CaseSensitive
                                              : Qt::CaseInsensitive) == 0;
}

EmotePtr EmoteAlias::createEmote() const
{
    if (this->word_.isEmpty() || this->link_.isEmpty())
    {
        return nullptr;
    }

    QUrl url(this->link_);
    if (!url.isValid())
    {
        return nullptr;
    }

    const auto host = url.host().toLower();
    const auto pathParts = url.path().split('/', Qt::SkipEmptyParts);

    // 1. 7TV URL format: https://7tv.app/emotes/<id> or https://7tv.app/emote/<id>
    if ((host == "7tv.app" || host.endsWith(".7tv.app")) &&
        pathParts.size() >= 2 &&
        (pathParts[0] == "emotes" || pathParts[0] == "emote"))
    {
        const auto &emoteId = pathParts[1];
        const auto u1 =
            Url{QStringLiteral("https://cdn.7tv.app/emote/%1/1x.webp")
                    .arg(emoteId)};
        const auto u2 =
            Url{QStringLiteral("https://cdn.7tv.app/emote/%1/2x.webp")
                    .arg(emoteId)};
        const auto u3 =
            Url{QStringLiteral("https://cdn.7tv.app/emote/%1/3x.webp")
                    .arg(emoteId)};
        const auto u4 =
            Url{QStringLiteral("https://cdn.7tv.app/emote/%1/4x.webp")
                    .arg(emoteId)};
        return std::make_shared<Emote>(Emote{
            .name = EmoteName{this->word_},
            .images = ImageSet{u1, u2, u4},
            .tooltip =
                Tooltip{QStringLiteral("Custom Alias: %1\n7TV Emote (%2)")
                            .arg(this->word_, emoteId)},
            .homePage = Url{this->link_},
        });
    }

    // 2. BetterTTV URL format: https://betterttv.com/emotes/<id> or https://betterttv.net/emote/<id>
    if ((host == "betterttv.com" || host.endsWith(".betterttv.com") ||
         host == "betterttv.net" || host.endsWith(".betterttv.net")) &&
        pathParts.size() >= 2 &&
        (pathParts[0] == "emotes" || pathParts[0] == "emote"))
    {
        const auto &emoteId = pathParts[1];
        const auto u1 =
            Url{QStringLiteral("https://cdn.betterttv.net/emote/%1/1x.webp")
                    .arg(emoteId)};
        const auto u2 =
            Url{QStringLiteral("https://cdn.betterttv.net/emote/%1/2x.webp")
                    .arg(emoteId)};
        const auto u3 =
            Url{QStringLiteral("https://cdn.betterttv.net/emote/%1/3x.webp")
                    .arg(emoteId)};
        return std::make_shared<Emote>(Emote{
            .name = EmoteName{this->word_},
            .images = ImageSet{u1, u2, u3},
            .tooltip =
                Tooltip{QStringLiteral("Custom Alias: %1\nBetterTTV Emote (%2)")
                            .arg(this->word_, emoteId)},
            .homePage = Url{this->link_},
        });
    }

    // 3. FrankerFaceZ URL format: https://www.frankerfacez.com/emoticon/<id>-<name>
    if ((host == "frankerfacez.com" || host.endsWith(".frankerfacez.com")) &&
        pathParts.size() >= 2 &&
        (pathParts[0] == "emoticon" || pathParts[0] == "emote"))
    {
        const auto emoteId = pathParts[1].section('-', 0, 0);
        const auto u1 =
            Url{QStringLiteral("https://cdn.frankerfacez.com/emoticon/%1/1")
                    .arg(emoteId)};
        const auto u2 =
            Url{QStringLiteral("https://cdn.frankerfacez.com/emoticon/%1/2")
                    .arg(emoteId)};
        const auto u4 =
            Url{QStringLiteral("https://cdn.frankerfacez.com/emoticon/%1/4")
                    .arg(emoteId)};
        return std::make_shared<Emote>(Emote{
            .name = EmoteName{this->word_},
            .images = ImageSet{u1, u2, u4},
            .tooltip = Tooltip{QStringLiteral(
                                   "Custom Alias: %1\nFrankerFaceZ Emote (%2)")
                                   .arg(this->word_, emoteId)},
            .homePage = Url{this->link_},
        });
    }

    // 4. Direct CDN or image link (e.g. cdn.7tv.app, cdn.betterttv.net, etc.)
    return std::make_shared<Emote>(Emote{
        .name = EmoteName{this->word_},
        .images = ImageSet{Url{this->link_}},
        .tooltip = Tooltip{QStringLiteral("Custom Alias: %1").arg(this->word_)},
        .homePage = Url{this->link_},
    });
}

}  // namespace chatterino
