// SPDX-FileCopyrightText: 2018 Contributors to Chatterino <https://chatterino.com>
//
// SPDX-License-Identifier: MIT

#pragma once

#include "common/FlagsEnum.hpp"
#include "common/UniqueAccess.hpp"
#include "controllers/highlights/HighlightCheck.hpp"
#include "messages/Message.hpp"
#include "singletons/Settings.hpp"

#include <boost/signals2/connection.hpp>
#include <pajlada/settings.hpp>
#include <pajlada/settings/settinglistener.hpp>
#include <pajlada/signals/signalholder.hpp>
#include <QColor>
#include <QHash>
#include <QStringList>
#include <QUrl>
#include <QUuid>

#include <cstdint>
#include <memory>
#include <utility>
#include <vector>

namespace chatterino {

class TwitchBadge;
struct MessageParseArgs;
class AccountController;
enum class MessageFlag : std::int64_t;
using MessageFlags = FlagsEnum<MessageFlag>;

/// Limerino: a HighlightCheck plus the ID of the group that produced it.
/// A null groupId means the check is *always global* (subscriptions,
/// whispers, self-highlight, reply threads, automod) and is included in
/// every channel's resolved set regardless of group membership.
struct GroupedHighlightCheck {
    HighlightCheck check;
    QUuid groupId;  // null = global
};

class HighlightController final
{
public:
    using SharedCheckVector =
        std::shared_ptr<const std::vector<HighlightCheck>>;

    HighlightController(Settings &settings, AccountController *accounts);

    [[nodiscard]] std::pair<bool, HighlightResult> check(
        const MessageParseArgs &args,
        const std::vector<TwitchBadge> &twitchBadges, const QString &senderName,
        const QString &originalMessage, const MessageFlags &messageFlags,
        MessagePlatform platform = MessagePlatform::AnyOrTwitch) const;

    /**
     * @brief Same as above, but scoped to the given channel key
     *        ("twitch:forsen", "kick:someone", "special:mentions"). An empty
     *        channel key resolves to global checks plus all `AllExcept {}`
     *        groups (legacy behaviour).
     **/
    [[nodiscard]] std::pair<bool, HighlightResult> check(
        const MessageParseArgs &args,
        const std::vector<TwitchBadge> &twitchBadges, const QString &senderName,
        const QString &originalMessage, const MessageFlags &messageFlags,
        MessagePlatform platform, const QString &channelKey) const;

private:
    void rebuildChecks(Settings &settings);

    /// Build, or fetch from cache, the checks applicable to @a channelKey.
    SharedCheckVector resolveChecks(Settings &settings,
                                    const QString &channelKey) const;

    /// Extracted the shared body so the legacy overload and the new
    /// channel-keyed overload share identical field-merging logic.
    std::pair<bool, HighlightResult> runChecks(
        const MessageParseArgs &args,
        const std::vector<TwitchBadge> &twitchBadges, const QString &senderName,
        const QString &originalMessage, const MessageFlags &messageFlags,
        MessagePlatform platform, const SharedCheckVector &checks) const;

    UniqueAccess<std::vector<GroupedHighlightCheck>> checks_;

    // Limerino: resolved caches. Both cleared together on any rebuild.
    mutable UniqueAccess<QHash<QString, SharedCheckVector>> channelChecks_;
    mutable UniqueAccess<QHash<QString, SharedCheckVector>> byGroupSet_;

    pajlada::SettingListener rebuildListener_;
    pajlada::Signals::SignalHolder signalHolder_;
    std::vector<boost::signals2::scoped_connection> bConnections;
};

}  // namespace chatterino
