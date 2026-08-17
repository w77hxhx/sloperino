// SPDX-FileCopyrightText: 2018 Contributors to Chatterino <https://chatterino.com>
//
// SPDX-License-Identifier: MIT

#include "providers/twitch/TwitchHelpers.hpp"

#include "common/QLogging.hpp"
#include "singletons/Settings.hpp"
#include "util/IrcHelpers.hpp"

namespace chatterino {

bool trimChannelName(const QString &channelName, QString &outChannelName)
{
    if (channelName.length() < 2)
    {
        qCDebug(chatterinoTwitch) << "channel name length below 2";
        return false;
    }

    outChannelName = channelName.mid(1);

    return true;
}

int stripLeadingReplyMention(Communi::TagsRef tags, QString &content)
{
    if (!getSettings()->stripReplyMention)
    {
        return 0;
    }
    if (getSettings()->hideReplyContext)
    {
        // Never strip reply mentions if reply contexts are hidden
        return 0;
    }

    if (auto optDisplayName = tags.get("reply-parent-display-name"))
    {
        auto displayName = parseTagString(*optDisplayName);

        if (content.length() <= 1 + displayName.length())
        {
            // The reply contains no content
            return 0;
        }

        if (content.startsWith('@') &&
            content.at(1 + displayName.length()) == ' ' &&
            content.indexOf(displayName, 1) == 1)
        {
            int messageOffset = 1 + displayName.length() + 1;
            content.remove(0, messageOffset);
            return messageOffset;
        }
    }
    return 0;
}

}  // namespace chatterino

