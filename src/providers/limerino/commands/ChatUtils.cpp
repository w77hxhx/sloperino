// SPDX-License-Identifier: MIT

#include "providers/limerino/commands/ChatUtils.hpp"

#include "common/Channel.hpp"
#include "controllers/commands/CommandContext.hpp"
#include "providers/limerino/gql/LimerinoGql.hpp"
#include "providers/limerino/gql/PersistedQueries.hpp"
#include "providers/limerino/LimerinoApi.hpp"
#include "providers/limerino/LimerinoAuth.hpp"
#include "providers/limerino/LimerinoErrors.hpp"
#include "providers/twitch/TwitchChannel.hpp"

#include <QJsonArray>
#include <QJsonObject>
#include <QRandomGenerator>
#include <QSet>

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

QString randomHex(int length)
{
    static const QString chars = QStringLiteral("0123456789abcdef");
    QString out;
    out.reserve(length);
    for (int i = 0; i < length; ++i)
    {
        out += chars[QRandomGenerator::global()->bounded(chars.size())];
    }
    return out;
}

constexpr int MAX_LOGS_PAGES = 100;  // 100k-message runaway cap

}  // namespace

QString resubNotification(const CommandContext &ctx)
{
    auto *tchan = ctx.twitchChannel;
    const ChannelPtr channel = ctx.channel;
    if (tchan == nullptr)
    {
        return QStringLiteral("/resub: only available in Twitch channels");
    }

    QString err;
    auto token = LimerinoAuth::resolveReadToken(&err);
    if (!token.hasToken())
    {
        say(ctx.channel,
            err.isEmpty() ? LimerinoAuth::errors::tokenRequiredMessage(
                                QStringLiteral("share a resub notification"))
                          : err);
        return {};
    }

    const QString message = ctx.words.mid(1).join(QStringLiteral(" "));
    gql::executeInline(
        QStringLiteral("ShareResub"), gql::SHARE_RESUB_MUTATION,
        QJsonObject{{QStringLiteral("input"),
                     QJsonObject{{QStringLiteral("channelLogin"),
                                  tchan->getName()},
                                 {QStringLiteral("includeStreak"), true},
                                 {QStringLiteral("message"), message}}}},
        token.token,
        [](const QJsonObject & /*data*/) {
            // Plugin prints nothing on success.
        },
        [weak = std::weak_ptr(channel)](const gql::GqlError &e) {
            if (auto chan = weak.lock())
            {
                chan->addSystemMessage(e.message);
            }
        },
        10000);
    return {};
}

QString cheer(const CommandContext &ctx)
{
    auto *tchan = ctx.twitchChannel;
    const ChannelPtr channel = ctx.channel;
    if (tchan == nullptr)
    {
        return QStringLiteral("/cheer: only available in Twitch channels");
    }

    bool ok = false;
    const int amount = ctx.words.value(1).toInt(&ok);
    if (!ok || amount <= 0)
    {
        say(ctx.channel, QStringLiteral("Please specify a valid cheer amount!"));
        return {};
    }

    QString err;
    auto token = LimerinoAuth::resolveReadToken(&err);
    if (!token.hasToken())
    {
        say(ctx.channel,
            err.isEmpty() ? LimerinoAuth::errors::tokenRequiredMessage(
                                QStringLiteral("cheer"))
                          : err);
        return {};
    }

    const QString message = ctx.words.mid(2).join(QStringLiteral(" "));
    gql::executePersisted(
        gql::PQ_SEND_CHEER,
        QJsonObject{{QStringLiteral("input"),
                     QJsonObject{{QStringLiteral("id"),
                                  randomHex(8) + "-" + randomHex(4) + "-" +
                                      randomHex(4) + "-" + randomHex(4) + "-" +
                                      randomHex(12)},
                                 {QStringLiteral("targetID"), tchan->roomId()},
                                 {QStringLiteral("bits"), amount},
                                 {QStringLiteral("content"),
                                  QStringLiteral("Cheer%1 %2")
                                      .arg(amount)
                                      .arg(message)},
                                 {QStringLiteral("isAutoModEnabled"), true},
                                 {QStringLiteral("shouldCheerAnyway"), false},
                                 {QStringLiteral("imageID"), QJsonValue{}},
                                 {QStringLiteral("pinCheer"), false}}}},
        token.token,
        [weak = std::weak_ptr(channel)](const QJsonObject &data) {
            const QJsonObject errr =
                data[QStringLiteral("sendCheer")].toObject()[
                    QStringLiteral("validationError")].toObject();
            if (auto chan = weak.lock())
            {
                if (!errr.isEmpty())
                {
                    chan->addSystemMessage(QStringLiteral("Error: %1 - %2")
                                               .arg(errr[QStringLiteral("code")]
                                                        .toString(),
                                                    errr[QStringLiteral("message")]
                                                        .toString()));
                }
                // silent success (plugin: callback(nil))
            }
        },
        [weak = std::weak_ptr(channel)](const gql::GqlError &e) {
            if (auto chan = weak.lock())
            {
                chan->addSystemMessage(
                    QStringLiteral("Error sending cheer! %1").arg(e.message));
            }
        });
    return {};
}

QString displayName(const CommandContext &ctx)
{
    const ChannelPtr channel = ctx.channel;
    const QString name = ctx.words.value(1);
    if (name.isEmpty())
    {
        say(ctx.channel, QStringLiteral("Usage: /displayname <your_display_name>"));
        return {};
    }

    QString err;
    auto token = LimerinoAuth::resolveCurrentUserToken(&err);
    if (!token.hasToken())
    {
        say(ctx.channel, err.isEmpty()
                             ? LimerinoAuth::errors::tokenRequiredMessage(
                                   QStringLiteral("update your display name"))
                             : err);
        return {};
    }

    gql::executeInline(
        QString(), gql::UPDATE_DISPLAY_NAME_MUTATION,
        QJsonObject{{QStringLiteral("input"),
                     QJsonObject{{QStringLiteral("displayName"), name},
                                 {QStringLiteral("userID"), token.userId}}}},
        token.token,
        [weak = std::weak_ptr(channel)](const QJsonObject &data) {
            if (auto chan = weak.lock())
            {
                const QString code =
                    data[QStringLiteral("updateUser")]
                        .toObject()[QStringLiteral("error")]
                        .toObject()[QStringLiteral("code")]
                        .toString();
                chan->addSystemMessage(
                    code.isEmpty()
                        ? QStringLiteral("Display name updated successfully!")
                        : QStringLiteral(
                              "Failed to send update display name! Status: %1")
                              .arg(code));
            }
        },
        [weak = std::weak_ptr(channel)](const gql::GqlError &e) {
            if (auto chan = weak.lock())
            {
                chan->addSystemMessage(
                    QStringLiteral("Failed to send update display name! %1")
                        .arg(e.message));
            }
        });
    return {};
}

QString logsExtended(const CommandContext &ctx)
{
    auto *tchan = ctx.twitchChannel;
    if (tchan == nullptr)
    {
        return QStringLiteral("/logsextended: only available in Twitch channels");
    }

    say(ctx.channel, QStringLiteral("Fetching user's logs, this may take a while!"));

    // Grammar: /logsextended [-id] [user] [channel]; 1 arg=user, 2 args=user+channel.
    QStringList args = ctx.words.mid(1);
    const bool withId = args.contains(QStringLiteral("-id"));
    args.removeAll(QStringLiteral("-id"));
    const QString userLogin = args.value(0, tchan->getName());
    const QString channelLogin = args.value(1, tchan->getName());

    QString err;
    auto token = LimerinoAuth::resolveReadToken(&err);
    if (!token.hasToken())
    {
        say(ctx.channel,
            err.isEmpty()
                ? LimerinoAuth::errors::tokenRequiredMessage(
                      QStringLiteral("fetch message logs"))
                : err);
        return {};
    }

    // Resolve both ids first (plugin does getUserId(x) -> getUserId(y)).
    const auto resolveChain = [channel = ctx.channel, token = token.token,
                               withId, channelLogin](const QString &userLogin) {
        const auto fetchIds = [channel, token, withId, channelLogin](
                                  const QString &id1, const QString &id2) {
            auto lines = std::make_shared<QStringList>();
            auto pages = std::make_shared<int>(0);
            auto page = std::make_shared<std::function<void(const QString &)>>();
            *page = [page, lines, pages, token, id1, id2, withId, channel,
                     channelLogin](const QString &cursor) {
                QJsonObject variables{
                    {QStringLiteral("senderID"), id1},
                    {QStringLiteral("channelID"), id2},
                    {QStringLiteral("cursor"), cursor.isEmpty()
                                                   ? QJsonValue()
                                                   : QJsonValue(cursor)},
                };
                gql::executeInline(
                    QStringLiteral("TCN_ViewerCardModLogsMessagesBySender"),
                    withId ? gql::VIEWER_CARD_MODLOG_MESSAGES_QUERY
                           : gql::VIEWER_CARD_MODLOG_MESSAGES_NOID_QUERY,
                    variables, token,
                    [page, lines, pages, withId, channel,
                     channelLogin](const QJsonObject &data) {
                        const QJsonObject logs =
                            data[QStringLiteral("viewerCardModLogs")].toObject();
                        if (logs.isEmpty())
                        {
                            if (auto chan = channel)
                            {
                                chan->addSystemMessage(
                                    QStringLiteral("Failed to fetch messages!"));
                            }
                            return;
                        }
                        const QJsonArray edges =
                            logs[QStringLiteral("messages")]
                                .toObject()[QStringLiteral("edges")]
                                .toArray();
                        for (const QJsonValue &v : edges)
                        {
                            const QJsonObject node =
                                v.toObject()[QStringLiteral("node")].toObject();
                            const QString text =
                                node[QStringLiteral("content")]
                                    .toObject()[QStringLiteral("text")]
                                    .toString();
                            if (text.isEmpty())
                            {
                                continue;
                            }
                            QString line =
                                node[QStringLiteral("sentAt")].toString().left(19) +
                                QStringLiteral(" ") +
                                node[QStringLiteral("sender")]
                                    .toObject()[QStringLiteral("login")]
                                    .toString() +
                                QStringLiteral(": ") + text;
                            if (node[QStringLiteral("isDeleted")].toBool())
                            {
                                line += QStringLiteral("\nDeleted by ") +
                                        node[QStringLiteral("lastUpdatedBy")]
                                            .toObject()[QStringLiteral("displayName")]
                                            .toString();
                            }
                            else if (withId)
                            {
                                line += QStringLiteral("\n") +
                                        node[QStringLiteral("id")].toString();
                            }
                            lines->append(line);
                        }
                        const QString nextCursor =
                            edges.isEmpty()
                                ? QString()
                                : edges.last()
                                      .toObject()[QStringLiteral("cursor")]
                                      .toString();
                        const bool hasNext =
                            logs[QStringLiteral("messages")]
                                .toObject()[QStringLiteral("pageInfo")]
                                .toObject()[QStringLiteral("hasNextPage")]
                                .toBool();
                        if (hasNext && !nextCursor.isEmpty() &&
                            ++(*pages) < MAX_LOGS_PAGES)
                        {
                            (*page)(nextCursor);
                        }
                        else
                        {
                            // plugin's final step: upload the whole log to the paste host.
                            LimerinoApi::uploadPaste(
                                QStringLiteral("Total messages: %1\n%2")
                                    .arg(lines->size())
                                    .arg(lines->join(QStringLiteral("\n"))),
                                [channel](const QString &url) {
                                    say(channel, url);
                                },
                                [channel](const QString &err) {
                                    say(channel, err);
                                });
                        }
                    },
                    [channel](const gql::GqlError &e) {
                        say(channel, e.message);
                    },
                    10000);
            };
            (*page)(QString());
        };

        // getUserId(x) then getUserId(y) sequences (plugin semantics).
        gql::executePersisted(
            gql::PQ_GET_USER_ID,
            QJsonObject{{QStringLiteral("login"), userLogin},
                        {QStringLiteral("lookupType"), QStringLiteral("ALL")}},
            token,
            [token, userLogin, channelLogin,
             fetchIds](const QJsonObject &data) {
                const QString id1 = data[QStringLiteral("user")]
                                            .toObject()[QStringLiteral("id")]
                                            .toString();
                gql::executePersisted(
                    gql::PQ_GET_USER_ID,
                    QJsonObject{{QStringLiteral("login"), channelLogin},
                                {QStringLiteral("lookupType"),
                                 QStringLiteral("ALL")}},
                    token,
                    [fetchIds, id1](const QJsonObject &data2) {
                        const QString id2 = data2[QStringLiteral("user")]
                                                    .toObject()[QStringLiteral("id")]
                                                    .toString();
                        fetchIds(id1, id2);
                    },
                    [](const gql::GqlError &) {});
            },
            [](const gql::GqlError &) {});
    };

    resolveChain(userLogin);
    return {};
}

}  // namespace chatterino::LimerinoCommands
