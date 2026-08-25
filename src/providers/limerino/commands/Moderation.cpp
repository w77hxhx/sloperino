// SPDX-License-Identifier: MIT

#include "providers/limerino/commands/Moderation.hpp"

#include "common/Channel.hpp"
#include "controllers/commands/CommandContext.hpp"
#include "providers/limerino/gql/LimerinoGql.hpp"
#include "providers/limerino/gql/PersistedQueries.hpp"
#include "providers/limerino/LimerinoAuth.hpp"
#include "providers/limerino/LimerinoErrors.hpp"
#include "providers/limerino/nuke/NukeExecutor.hpp"
#include "providers/twitch/TwitchChannel.hpp"
#include "widgets/dialogs/limerino/LimerinoModLogsDialog.hpp"

#include <QJsonObject>

#include <memory>

namespace chatterino::LimerinoCommands {

namespace gql = chatterino::LimerinoAuth::gql;

namespace {

void say(const ChannelPtr &channel, const QString &text)
{
    if (channel)
    {
        channel->addSystemMessage(text);
    }
}

void openModLogs(const ChannelPtr &channel, const QString &channelId,
                 const QString &userLabel, int days)
{
    QString err;
    auto token = LimerinoAuth::resolveModerationToken(channelId,
                                                      channel->getName(), &err);
    if (!token.hasToken())
    {
        say(channel,
            err.isEmpty()
                ? LimerinoAuth::errors::tokenRequiredMessage(
                      QStringLiteral("view moderator actions"))
                : err);
        return;
    }
    auto *dialog = new limerino::LimerinoModLogsDialog(
        channel->getName(), channelId, userLabel, days);
    dialog->show();
}

}  // namespace

QString modlogs(const CommandContext &ctx)
{
    auto *tchan = ctx.twitchChannel;
    if (tchan == nullptr)
    {
        return QStringLiteral("/modlogs: only available in Twitch channels");
    }

    // Plugin grammar: /modlogs [user] [days|"all"]; default channel, 30 days.
    const QString arg1 = ctx.words.value(1);
    const QString arg2 = ctx.words.value(2);

    QString user;
    int days = 30;
    if (!arg1.isEmpty() && arg2.isEmpty())
    {
        bool ok = false;
        const int n = arg1.toInt(&ok);
        if (ok)
        {
            days = n;
        }
        else
        {
            user = arg1;
            days = 30;
        }
    }
    else
    {
        user = arg1;
        bool ok = false;
        if (arg2 == QLatin1String("all"))
        {
            days = 10000;
        }
        else
        {
            days = arg2.toInt(&ok, 10);
            if (!ok || days <= 0)
            {
                days = 30;
            }
        }
    }
    if (days <= 0)
    {
        days = 30;
    }

    const ChannelPtr channel = ctx.channel;
    if (user.isEmpty())
    {
        say(channel, QStringLiteral("Fetching mod actions summary!"));
        openModLogs(channel, tchan->roomId(), tchan->getName(), days);
        return {};
    }

    // user-given: resolve their id first, then open the dialog filtered
    // by that display name (the plugin resolves getUserId first as well)
    QString err;
    auto token = LimerinoAuth::resolveModerationToken(
        tchan->roomId(), tchan->getName(), &err);
    if (!token.hasToken())
    {
        say(channel, err.isEmpty()
                         ? LimerinoAuth::errors::tokenRequiredMessage(
                               QStringLiteral("view moderator actions"))
                         : err);
        return {};
    }
    gql::executePersisted(
        gql::PQ_GET_USER_ID,
        QJsonObject{{QStringLiteral("login"), user},
                    {QStringLiteral("lookupType"), QStringLiteral("ALL")}},
        token.token,
        [weak = std::weak_ptr(channel), user, days, channelId = tchan->roomId()](
            const QJsonObject &data) {
            if (auto chan = weak.lock())
            {
                openModLogs(chan, channelId, user, days);
            }
        },
        [weak = std::weak_ptr(channel)](const gql::GqlError &e) {
            if (auto chan = weak.lock())
            {
                chan->addSystemMessage(e.message);
            }
        });
    return {};
}

QString acknowledgeWarning(const CommandContext &ctx)
{
    auto *tchan = ctx.twitchChannel;
    const ChannelPtr channel = ctx.channel;
    if (tchan == nullptr)
    {
        return QStringLiteral("/acknowledgewarning: only available in Twitch channels");
    }

    QString err;
    auto token = LimerinoAuth::resolveReadToken(&err);
    if (!token.hasToken())
    {
        say(ctx.channel,
            err.isEmpty()
                ? LimerinoAuth::errors::tokenRequiredMessage(
                      QStringLiteral("acknowledge a chat warning"))
                : err);
        return {};
    }

    gql::executePersisted(
        gql::PQ_ACKNOWLEDGE_CHAT_WARNING,
        QJsonObject{{QStringLiteral("input"),
                     QJsonObject{{QStringLiteral("channelID"),
                                  tchan->roomId()}}}},
        token.token,
        [weak = std::weak_ptr(channel)](const QJsonObject &data) {
            if (auto chan = weak.lock())
            {
                const QString code =
                    data[QStringLiteral("acknowledgeChatWarning")]
                        .toObject()[QStringLiteral("error")]
                        .toObject()[QStringLiteral("code")]
                        .toString();
                chan->addSystemMessage(
                    code.isEmpty()
                        ? QStringLiteral("Successfully acknowledged warning.")
                        : code);
            }
        },
        [weak = std::weak_ptr(channel)](const gql::GqlError &e) {
            if (auto chan = weak.lock())
            {
                chan->addSystemMessage(e.message);
            }
        });
    return {};
}

QString cancelNuke(const CommandContext &ctx)
{
    const QString result = limerino::cancelRunningNuke();
    if (ctx.channel)
    {
        ctx.channel->addSystemMessage(result);
    }
    return {};
}

}  // namespace chatterino::LimerinoCommands
