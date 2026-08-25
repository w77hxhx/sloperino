// SPDX-License-Identifier: MIT

#include "providers/limerino/commands/Pins.hpp"

#include "Application.hpp"
#include "common/Channel.hpp"
#include "controllers/commands/CommandContext.hpp"
#include "messages/Message.hpp"
#include "providers/limerino/gql/LimerinoGql.hpp"
#include "providers/limerino/gql/PersistedQueries.hpp"
#include "providers/limerino/LimerinoAuth.hpp"
#include "providers/limerino/LimerinoErrors.hpp"
#include "providers/twitch/api/Helix.hpp"
#include "providers/twitch/TwitchChannel.hpp"
#include "singletons/WindowManager.hpp"
#include "widgets/Notebook.hpp"
#include "widgets/Window.hpp"
#include "widgets/dialogs/limerino/LimerinoPinView.hpp"
#include "widgets/splits/Split.hpp"

#include <QJsonArray>
#include <QJsonObject>
#include <QPointer>

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

// Plugin's pinnedmessageID / pinnedmessage: persisted GetPinnedChat,
// variables {channelID, count: 5}. Only edges[1] matters to it.
void fetchPinnedChat(
    const QString &token, const QString &channelId,
    const std::function<void(const QJsonObject &node)> &onFound,
    const std::function<void(const QString &err)> &onErr)
{
    gql::executePersisted(
        gql::PQ_GET_PINNED_CHAT,
        QJsonObject{{QStringLiteral("channelID"), channelId},
                    {QStringLiteral("count"), 5}},
        token,
        [onFound, onErr](const QJsonObject &data) {
            const QJsonArray edges =
                data[QStringLiteral("channel")]
                    .toObject()[QStringLiteral("pinnedChatMessages")]
                    .toObject()[QStringLiteral("edges")]
                    .toArray();
            if (edges.isEmpty())
            {
                if (onErr)
                {
                    onErr(QStringLiteral("Channel has no pinned message"));
                }
                return;
            }
            if (onFound)
            {
                onFound(edges.first().toObject()[QStringLiteral("node")]
                            .toObject());
            }
        },
        [onErr](const gql::GqlError &e) {
            if (onErr)
            {
                onErr(e.message);
            }
        });
}

}  // namespace

QString pinMessage(const CommandContext &ctx)
{
    auto *tchan = ctx.twitchChannel;
    const ChannelPtr channel = ctx.channel;
    if (tchan == nullptr)
    {
        return QStringLiteral("/pinmessage: only available in Twitch channels");
    }

    const QString messageId = ctx.words.value(1);
    if (messageId.isEmpty())
    {
        say(ctx.channel, QStringLiteral("Usage: /pinmessage <message id>"));
        return {};
    }

    QString err;
    auto token = LimerinoAuth::resolveModerationToken(
        tchan->roomId(), tchan->getName(), &err);
    if (!token.hasToken())
    {
        say(ctx.channel,
            err.isEmpty()
                ? LimerinoAuth::errors::tokenRequiredMessage(
                      QStringLiteral("pin messages in this channel"))
                : err);
        return {};
    }

    gql::executePersisted(
        gql::PQ_PIN_CHAT_MESSAGE,
        QJsonObject{{QStringLiteral("input"),
                     QJsonObject{{QStringLiteral("channelID"), tchan->roomId()},
                                 {QStringLiteral("messageID"), messageId},
                                 {QStringLiteral("type"),
                                  QStringLiteral("MOD")}}}},
        token.token,
        [                        weak = std::weak_ptr(channel)](const QJsonObject &data) {
            if (auto chan = weak.lock())
            {
                chan->addSystemMessage(QStringLiteral("Message pinned successfully!"));
            }
        },
        [                        weak = std::weak_ptr(channel)](const gql::GqlError &e) {
            if (auto chan = weak.lock())
            {
                chan->addSystemMessage(
                    QStringLiteral("Error while pinning message: %1")
                        .arg(e.message));
            }
        });
    return {};
}

QString sendPinnedMessage(const CommandContext &ctx)
{
    auto *tchan = ctx.twitchChannel;
    const ChannelPtr channel = ctx.channel;
    if (tchan == nullptr)
    {
        return QStringLiteral("/sendpinnedmessage: only available in Twitch channels");
    }

    const QString text = ctx.words.mid(1).join(QStringLiteral(" "));
    if (text.isEmpty())
    {
        say(ctx.channel, QStringLiteral("Usage: /sendpinnedmessage <text>"));
        return {};
    }

    QString err;
    auto token = LimerinoAuth::resolveModerationToken(
        tchan->roomId(), tchan->getName(), &err);
    if (!token.hasToken())
    {
        say(ctx.channel,
            err.isEmpty()
                ? LimerinoAuth::errors::tokenRequiredMessage(
                      QStringLiteral("send pinned messages in this channel"))
                : err);
        return {};
    }

    gql::executePersisted(
        gql::PQ_SEND_PINNED_CHAT_MESSAGE,
        QJsonObject{{QStringLiteral("input"),
                     QJsonObject{{QStringLiteral("channelID"), tchan->roomId()},
                                 {QStringLiteral("messageText"), text}}}},
        token.token,
        [                        weak = std::weak_ptr(channel)](const QJsonObject &data) {
            if (auto chan = weak.lock())
            {
                chan->addSystemMessage(QStringLiteral("Sent pinned message successfully!"));
            }
        },
        [                        weak = std::weak_ptr(channel)](const gql::GqlError &e) {
            if (auto chan = weak.lock())
            {
                chan->addSystemMessage(
                    QStringLiteral("Failed to send pinned message! Status: %1")
                        .arg(e.message));
            }
        });
    return {};
}

QString unpinMessage(const CommandContext &ctx)
{
    auto *tchan = ctx.twitchChannel;
    const ChannelPtr channel = ctx.channel;
    if (tchan == nullptr)
    {
        return QStringLiteral("/unpin: only available in Twitch channels");
    }

    QString err;
    auto token = LimerinoAuth::resolveModerationToken(
        tchan->roomId(), tchan->getName(), &err);
    if (!token.hasToken())
    {
        say(ctx.channel,
            err.isEmpty()
                ? LimerinoAuth::errors::tokenRequiredMessage(
                      QStringLiteral("unpin messages in this channel"))
                : err);
        return {};
    }

    fetchPinnedChat(
        token.token, tchan->roomId(),
        [                        weak = std::weak_ptr(channel), token = token.token,
         channelId = tchan->roomId()](const QJsonObject &node) {
            const QString id = node[QStringLiteral("id")].toString();
            auto chan = weak.lock();
            if (!chan || id.isEmpty())
            {
                return;
            }
            gql::executePersisted(
                gql::PQ_UNPIN_CHAT_MESSAGE,
                QJsonObject{{QStringLiteral("input"),
                             QJsonObject{{QStringLiteral("id"), id},
                                         {QStringLiteral("reason"),
                                          QStringLiteral("UNPIN")}}}},
                token,
                [weak](const QJsonObject & /*data*/) {
                    if (auto ch = weak.lock())
                    {
                        ch->addSystemMessage(
                            QStringLiteral("Unpinned message successfully!"));
                    }
                },
                [weak](const gql::GqlError &e) {
                    if (auto ch = weak.lock())
                    {
                        ch->addSystemMessage(
                            QStringLiteral("Failed to unpin message! Status: %1")
                                .arg(e.message));
                    }
                });
        },
        [                        weak = std::weak_ptr(channel)](const QString &e) {
            if (auto chan = weak.lock())
            {
                chan->addSystemMessage(e);
            }
        });
    return {};
}

QString viewPin(const CommandContext &ctx)
{
    auto *tchan = ctx.twitchChannel;
    const ChannelPtr channel = ctx.channel;
    if (tchan == nullptr)
    {
        return QStringLiteral("/viewpin: only available in Twitch channels");
    }

    QString err;
    auto token = LimerinoAuth::resolveReadToken(&err);
    if (!token.hasToken())
    {
        say(ctx.channel,
            err.isEmpty() ? LimerinoAuth::errors::tokenRequiredMessage(
                                QStringLiteral("view the pinned message"))
                          : err);
        return {};
    }

    fetchPinnedChat(
        token.token, tchan->roomId(),
        [channel = ctx.channel, tchan, token](const QJsonObject &node) {
            if (!channel)
            {
                return;
            }
            const QString id = node[QStringLiteral("id")].toString();
            const QJsonObject pinned =
                node[QStringLiteral("pinnedMessage")].toObject();
            const QString sender =
                pinned[QStringLiteral("sender")].toObject()[
                    QStringLiteral("displayName")].toString();
            const QString text = pinned[QStringLiteral("content")]
                                     .toObject()[QStringLiteral("text")]
                                     .toString();
            const QString pinnedBy =
                node[QStringLiteral("pinnedBy")].toObject()[
                    QStringLiteral("displayName")].toString();

            // Pin time + pinner from the upstream Helix state when it
            // corresponds to the same message (its starts_at is the only
            // source for "when it was pinned" - the GQL op has no such field).
            QDateTime pinnedAt;
            QString pinnedByFromHelix;
            if (const auto *helixPinned = tchan->getPinnedMessage())
            {
                if (helixPinned->messageID == id)
                {
                    pinnedAt = helixPinned->startsAt;
                    pinnedByFromHelix = helixPinned->pinnedBy.displayName;
                }
            }

            // Find the message in local history for exact chat rendering.
            MessagePtr found;
            for (const MessagePtr &msg : channel->getMessageSnapshot())
            {
                if (msg->id == id)
                {
                    found = msg;
                    break;
                }
            }

            // (Re)mount or update the banner in the current selected split.
            static QPointer<limerino::LimerinoPinView> active;
            static QPointer<Split> activeSplit;
            auto *page = getApp()->getWindows()
                             ->getMainWindow()
                             .getNotebook()
                             .getSelectedPage();
            auto *split = page != nullptr ? page->getSelectedSplit()
                                          : nullptr;
            if (split == nullptr)
            {
                return;
            }
            if (active && split != activeSplit)
            {
                active->dismiss();
                active = nullptr;
                activeSplit = nullptr;
            }
            if (!active)
            {
                active = new limerino::LimerinoPinView(split);
                activeSplit = split;
            }
            const QString shownBy =
                !pinnedByFromHelix.isEmpty() ? pinnedByFromHelix : pinnedBy;
            if (found)
            {
                active->setMessage(found, shownBy, pinnedAt);
            }
            else
            {
                active->setPlainMessage(sender, text, shownBy, pinnedAt);
            }
        },
        [                        weak = std::weak_ptr(channel)](const QString &e) {
            if (auto chan = weak.lock())
            {
                chan->addSystemMessage(e);
            }
        });
    return {};
}

}  // namespace chatterino::LimerinoCommands
