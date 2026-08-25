// SPDX-License-Identifier: MIT

#include "providers/limerino/nuke/NukeEngine.hpp"

#include "Application.hpp"
#include "common/Channel.hpp"
#include "controllers/accounts/AccountController.hpp"
#include "messages/Message.hpp"
#include "messages/MessageFlag.hpp"
#include "providers/kick/KickAccount.hpp"
#include "providers/kick/KickAccountManager.hpp"
#include "providers/limerino/matcher/LimerinoMatcher.hpp"
#include "providers/limerino/matcher/LimerinoMatcherValidation.hpp"
#include "providers/twitch/TwitchAccount.hpp"
#include "providers/twitch/TwitchAccountManager.hpp"

namespace chatterino::limerino {

namespace {

/// Anything in the buffer that is not a plain chat message. Disabled means the
/// message was already deleted for everyone (CLEARMSG / timeout): re-acting on
/// it is a wasted, rate-limited call.
constexpr MessageFlags EXCLUDED_FLAGS{MessageFlag::System, MessageFlag::Timeout,
                                      MessageFlag::Whisper,
                                      MessageFlag::ClearChat,
                                      MessageFlag::ModerationAction,
                                      MessageFlag::Disabled};

}  // namespace

QString nukeSelfLogin(const Channel &channel)
{
    switch (channel.messagePlatform())
    {
        case MessagePlatform::AnyOrTwitch: {
            auto user = getApp()->getAccounts()->twitch.getCurrent();
            if (!user->isAnon())
            {
                return user->getUserName().toLower();
            }
            return {};
        }

        case MessagePlatform::Kick: {
            return getApp()->getAccounts()->kick.current()->username().toLower();
        }
    }
    return {};
}

std::optional<QString> validateNukeRequest(const LimerinoMatcher &content,
                                           const LimerinoMatcher &sender,
                                           int lookbackSeconds)
{
    if (auto err = validateMatcherPair(content, sender))
    {
        return err;
    }
    if (lookbackSeconds <= 0)
    {
        return QStringLiteral(
            "Lookback must be a positive number of seconds.");
    }
    return std::nullopt;
}

NukePlan buildPlan(const std::vector<MessagePtr> &snapshot,
                   MessagePlatform platform, const QString &channelName,
                   const QString &selfLogin, const LimerinoMatcher &content,
                   const LimerinoMatcher &sender, int lookbackSeconds,
                   NukeAction action, const QDateTime &now)
{
    NukePlan plan;

    if (validateNukeRequest(content, sender, lookbackSeconds).has_value())
    {
        // The dialog validates before calling us; double-fail safe, never crash.
        plan.warnings.append(QStringLiteral(
            "nuke request is invalid; validate before building the plan"));
        return plan;
    }

    plan.messagesScanned = static_cast<int>(snapshot.size());

    if (snapshot.empty())
    {
        plan.warnings.append(QStringLiteral("channel buffer is empty"));
        return plan;
    }

    plan.bufferOldest = snapshot.front()->serverReceivedTime;
    plan.bufferNewest = snapshot.back()->serverReceivedTime;

    // If the oldest buffered message is *newer* than the requested window
    // start, the buffer does not cover the window — the nuke reaches less far
    // than the user asked. The dialog must surface this prominently.
    const auto windowStart = now.addSecs(-lookbackSeconds);
    plan.lookbackExceedsBuffer = plan.bufferOldest > windowStart;

    const QString broadcasterLogin = channelName.toLower();

    QHash<QString, qsizetype> targetIndexByLogin;
    int matchedWithoutId = 0;

    for (const auto &msg : snapshot)
    {
        if (msg->flags.hasAny(EXCLUDED_FLAGS))
        {
            continue;
        }
        if (msg->loginName.isEmpty())
        {
            continue;
        }
        if (msg->serverReceivedTime < windowStart)
        {
            continue;  // outside the lookback window
        }
        if (!content.matches(msg->messageText) ||
            !sender.matches(msg->loginName))
        {
            continue;
        }

        plan.messagesMatched += 1;

        const bool considerSender =
            action == NukeAction::Ban || action == NukeAction::Timeout ||
            action == NukeAction::Warn ||
            action == NukeAction::DeleteAndTimeout;
        const bool considerMessage = action == NukeAction::Delete ||
                                     action == NukeAction::DeleteAndTimeout;

        if (considerSender)
        {
            const auto login = msg->loginName.toLower();
            // Hard-coded per spec: never target the operator or the broadcaster.
            if (login != selfLogin && login != broadcasterLogin)
            {
                auto it = targetIndexByLogin.find(login);
                if (it == targetIndexByLogin.end())
                {
                    NukeTarget t;
                    t.userId = msg->userID;
                    t.login = login;
                    t.displayName = msg->displayName;
                    t.matchedMessages = 1;
                    plan.targets.append(t);
                    targetIndexByLogin.insert(login, plan.targets.size() - 1);
                }
                else
                {
                    plan.targets[*it].matchedMessages += 1;
                }
            }
        }

        if (considerMessage)
        {
            if (msg->id.isEmpty())
            {
                matchedWithoutId += 1;
            }
            else
            {
                plan.messageIds.append(msg->id);
            }
        }
    }

    if (matchedWithoutId > 0)
    {
        plan.warnings.append(QStringLiteral(
            "%1 matching message(s) had no message ID and could not be "
            "deleted; they were skipped")
                .arg(matchedWithoutId));
    }

    if (action == NukeAction::Warn && platform == MessagePlatform::Kick)
    {
        // Kick has no warn endpoint. Producing zero targets is correct; the
        // dialog disables warn on Kick channels before this can be reached.
        plan.warnings.append(QStringLiteral(
            "warn is not available on Kick; no targets produced"));
        plan.targets.clear();
    }

    return plan;
}

}  // namespace chatterino::limerino
