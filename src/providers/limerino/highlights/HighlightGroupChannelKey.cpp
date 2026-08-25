// SPDX-License-Identifier: MIT

#include "providers/limerino/highlights/HighlightGroupChannelKey.hpp"

#include "common/Channel.hpp"
#include "messages/Message.hpp"  // for MessagePlatform

#include <QStringBuilder>

namespace chatterino::limerino {

QString highlightChannelKey(const Channel &channel)
{
    switch (channel.getType())
    {
        case Channel::Type::TwitchWhispers:
            return QStringLiteral("special:whispers");
        case Channel::Type::TwitchMentions:
            return QStringLiteral("special:mentions");
        case Channel::Type::TwitchLive:
            return QStringLiteral("special:live");
        case Channel::Type::TwitchAutomod:
            return QStringLiteral("special:automod");
        case Channel::Type::Misc:
            return QStringLiteral("special:misc:") %
                   channel.getName().toLower();
        case Channel::Type::None:
        case Channel::Type::Direct:
        case Channel::Type::Multi:
            // Non-chat / composite channels: use a clearly-scoped sentinel so
            // they never accidentally match a real "twitch:<name>" entry.
            return QStringLiteral("special:other:") %
                   channel.getName().toLower();
        case Channel::Type::Twitch:
            return QStringLiteral("twitch:") % channel.getName().toLower();
        case Channel::Type::Kick:
            return QStringLiteral("kick:") % channel.getName().toLower();
        case Channel::Type::TwitchWatching:
            // Watching channel is a real Twitch chat; treat as a normal
            // Twitch channel key.
            return QStringLiteral("twitch:") % channel.getName().toLower();
        case Channel::Type::TwitchEnd:
            return QStringLiteral("special:other:") %
                   channel.getName().toLower();
    }
    return QStringLiteral("special:other:") % channel.getName().toLower();
}

QString highlightChannelKey(MessagePlatform platform, const QString &name)
{
    const auto lowered = name.toLower();
    switch (platform)
    {
        case MessagePlatform::Kick:
            return QStringLiteral("kick:") % lowered;
        case MessagePlatform::AnyOrTwitch:
            return QStringLiteral("twitch:") % lowered;
    }
    return QStringLiteral("twitch:") % lowered;
}

}  // namespace chatterino::limerino
