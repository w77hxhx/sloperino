// SPDX-License-Identifier: MIT

#include "providers/limerino/autoactions/LimerinoAutoActionRuntime.hpp"

#include "common/Channel.hpp"
#include "messages/Message.hpp"
#include "providers/kick/KickChannel.hpp"
#include "providers/limerino/autoactions/AutoActionPlaceholders.hpp"
#include "providers/limerino/autoactions/LimerinoAutoAction.hpp"
#include "providers/limerino/autoactions/LimerinoAutoActionController.hpp"
#include "providers/twitch/TwitchChannel.hpp"

#include "Application.hpp"
#include "controllers/accounts/AccountController.hpp"
#include "controllers/commands/CommandController.hpp"
#include "providers/kick/KickAccount.hpp"
#include "providers/kick/KickAccountManager.hpp"
#include "providers/twitch/TwitchAccount.hpp"
#include "providers/twitch/TwitchAccountManager.hpp"

#include "common/QLogging.hpp"

#include <QDateTime>
#include <QHash>
#include <QScopeGuard>
#include <QUuid>

namespace chatterino::limerino {

namespace {

/// One shot-fire gate per rule. Cooldowns persist across rule reloads unless
/// the rule itself is deleted (see rulesChanged wiring in installAutoActionRuntime).
struct CooldownBucket {
    QHash<QString /*rule id*/, QDateTime> lastFired;
};
CooldownBucket gCooldowns;

/// The runtime's one re-entrancy guard. The no-re-entry rule is that enqueue
/// → execCommand is synchronous inside evaluate. Anything this evaluation
/// triggers (system message additions to the channel, further evaluation on
/// a message emitted by the action) is prevented by setting `inEvaluation` to
/// true for the duration of this function.
bool gInEvaluation = false;

QString channelIdSafe(Channel &channel)
{
    if (auto *t = dynamic_cast<TwitchChannel *>(&channel))
    {
        return t->roomId();
    }
    if (auto *k = dynamic_cast<KickChannel *>(&channel))
    {
        return QString::number(k->userID());
    }
    return {};
}

bool isModeratorIn(const Channel &channel)
{
    // hasModRights is the rendered gating surface; call sites keep this free
    // of channel-type branches.
    return channel.hasModRights();
}

}  // namespace

void evaluateAutoActions(const MessagePtr &message, Channel &channel)
{
    if (!message || gInEvaluation)
    {
        return;
    }

    // Never act on one's own messages — the failed-command loop a bad rule
    // can produce ("/ban me" → "you banned me" → re-evaluation) is broken
    // here even when the matcher would otherwise pass.
    const QString platform = [&] {
        if (channel.isKickChannel())
        {
            return QStringLiteral("kick");
        }
        return QStringLiteral("twitch");
    }();

    const QString senderLower = message->loginName;
    if (senderLower.isEmpty())
    {
        return;
    }
    const QString selfLogin =
        channel.isKickChannel()
            ? getApp()->getAccounts()->kick.current()->username().toLower()
            : getApp()->getAccounts()->twitch.getCurrent()->getUserName()
                  .toLower();
    if (senderLower == selfLogin)
    {
        return;
    }

    if (!isModeratorIn(channel))
    {
        // Not a mod here — every action would be a wasted failing call.
        // Log loud once per evaluation per channel per setting-change round.
        return;
    }

    auto *controller = LimerinoAutoActionController::instance();
    if (controller == nullptr)
    {
        // Settings-only builds (unit tests): no rules to evaluate.
        return;
    }

    const QString channelKey = platform + QStringLiteral(":") +
                               channel.getName().toLower();
    const auto rules = controller->resolve(channelKey);
    if (rules->empty())
    {
        return;
    }

    gInEvaluation = true;
    const auto guard = qScopeGuard([] {
        gInEvaluation = false;
    });

    const QDateTime now = QDateTime::currentDateTime();
    for (const auto &rule : *rules)
    {
        if (!rule.enabled)
        {
            continue;
        }

        if (!rule.content.matches(message->messageText) ||
            !rule.sender.matches(senderLower))
        {
            continue;
        }

        // Per-rule cooldown (0 = no cooldown).
        if (rule.cooldownSeconds > 0)
        {
            const auto &lastFired = gCooldowns.lastFired[rule.id.toString()];
            if (lastFired.isValid() &&
                lastFired.addSecs(rule.cooldownSeconds) > now)
            {
                continue;
            }
        }

        AutoActionContext ctx;
        ctx.msgId = message->id;
        ctx.senderLogin = senderLower;
        ctx.senderDisplayName = message->displayName.isEmpty() ? senderLower
                                                               : message->displayName;
        ctx.senderId = message->userID;
        ctx.channelName = channel.getName();
        ctx.channelId = channelIdSafe(channel);
        ctx.platform = platform;

        const auto command = expandAutoAction(rule.action, ctx);
        if (!command.has_value() || command->isEmpty())
        {
            qCWarning(chatterinoMessage)
                << "Auto-action" << rule.name << "skipped: placeholder value"
                   "unavailable";
            continue;
        }

        gCooldowns.lastFired[rule.id.toString()] = now;

        qCDebug(chatterinoMessage)
            << "Auto-action fire:" << rule.name << "on message"
            << message->id << "->" << *command;

        // execCommand does the whole dispatch: /ban lands a Helix call, user
        // commands resolve, and any non-command text comes back to be echoed.
        // (It is also what the split's chat-mode menu uses verbatim.)
        getApp()->getCommands()->execCommand(*command,
                                             channel.shared_from_this(), false);

        // A rule that fires sends exactly one command; "all matching rules" is
        // per message traversal, so keep scanning for later rules.
    }
}

void LimerinoAutoActionRuntime_resetCooldowns()
{
    gCooldowns.lastFired.clear();
}

}  // namespace chatterino::limerino
