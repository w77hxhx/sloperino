// SPDX-License-Identifier: MIT

#include "providers/limerino/commands/Follows.hpp"

#include "Application.hpp"
#include "common/Channel.hpp"
#include "controllers/accounts/AccountController.hpp"
#include "controllers/commands/CommandContext.hpp"
#include "providers/limerino/gql/LimerinoGql.hpp"
#include "providers/limerino/gql/PersistedQueries.hpp"
#include "providers/limerino/LimerinoAuth.hpp"
#include "providers/limerino/LimerinoErrors.hpp"
#include "providers/twitch/TwitchAccount.hpp"
#include "providers/twitch/TwitchAccountManager.hpp"
#include "providers/twitch/TwitchChannel.hpp"
#include "providers/twitch/TwitchIrcServer.hpp"
#include "singletons/WindowManager.hpp"
#include "widgets/Notebook.hpp"
#include "widgets/Window.hpp"
#include "widgets/dialogs/limerino/LimerinoResultDialog.hpp"
#include "widgets/dialogs/limerino/LimerinoResultList.hpp"
#include "widgets/splits/Split.hpp"
#include "widgets/splits/SplitContainer.hpp"

#include <QDateTime>
#include <QDesktopServices>
#include <QJsonArray>
#include <QJsonObject>
#include <QMenu>
#include <QSet>
#include <QUrl>

#include <memory>

namespace chatterino::LimerinoCommands {

namespace gql = chatterino::LimerinoAuth::gql;

namespace {

// Hard caps per batch-2 spec: 10k rows => 100 pages of 100.
constexpr int MAX_FOLLOW_PAGES = 100;

void say(const ChannelPtr &channel, const QString &text)
{
    if (channel)
    {
        channel->addSystemMessage(text);
    }
}

struct FollowRow {
    QString login;
    QString followedAt;  // ISO timestamp
    bool notificationsOn = false;
    bool live = false;
    QString game;
    int viewers = 0;
};

// Plugin: inline gql query "follows" (channels FOLLOWED by login) and
// "followers" (the login's follower list), paged with edge.cursor, ASC order.
void fetchFollows(
    const QString &login, bool followersMode, const QString &token,
    const std::function<void(const QVector<FollowRow> &rows)> &onRows,
    const std::function<void(const QString &err)> &onErr,
    const std::function<void(int pageCount, int rowCount)> &onProgress)
{
    auto rows = std::make_shared<QVector<FollowRow>>();
    auto seenCursors = std::make_shared<QSet<QString>>();
    auto pages = std::make_shared<int>(0);

    auto page = std::make_shared<std::function<void(const QString &)>>();
    *page = [page, rows, seenCursors, pages, login, token, followersMode,
             onRows, onErr, onProgress](const QString &cursor) {
        QJsonObject variables{
            {QStringLiteral("login"), login},
            {QStringLiteral("cursor"), cursor.isEmpty() ? QJsonValue()
                                                        : QJsonValue(cursor)},
            {QStringLiteral("order"), QStringLiteral("ASC")},
        };
        gql::executeInline(
            followersMode ? QStringLiteral("followers")
                          : QStringLiteral("follows"),
            followersMode ? gql::FOLLOWERS_QUERY : gql::FOLLOWS_QUERY,
            variables, token,
            [page, rows, seenCursors, pages, followersMode, onRows, onErr,
             onProgress](const QJsonObject &data) {
                const QJsonObject conn =
                    data[QStringLiteral("user")]
                        .toObject()[followersMode
                                        ? QStringLiteral("followers")
                                        : QStringLiteral("follows")]
                        .toObject();
                const QJsonArray edges = conn[QStringLiteral("edges")].toArray();
                if (edges.isEmpty())
                {
                    onRows(*rows);
                    return;
                }
                for (const QJsonValue &v : edges)
                {
                    const QJsonObject edge = v.toObject();
                    FollowRow row;
                    row.followedAt =
                        edge[QStringLiteral("followedAt")].toString();
                    row.notificationsOn =
                        edge[QStringLiteral("notificationSettings")]
                            .toObject()[QStringLiteral("isEnabled")]
                            .toBool();
                    const QJsonObject node =
                        edge[QStringLiteral("node")].toObject();
                    row.login = node[QStringLiteral("login")].toString();
                    const QJsonObject stream =
                        node[QStringLiteral("stream")].toObject();
                    if (!stream.isEmpty())
                    {
                        row.live = true;
                        row.viewers =
                            stream[QStringLiteral("viewersCount")].toInt();
                        row.game = stream[QStringLiteral("game")]
                                       .toObject()[QStringLiteral("displayName")]
                                       .toString();
                    }
                    rows->append(std::move(row));
                }
                if (onProgress)
                {
                    onProgress(int(*pages) + 1, int(rows->size()));
                }

                const QString nextCursor =
                    edges.last().toObject()[QStringLiteral("cursor")].toString();
                ++(*pages);
                const bool repeated = !nextCursor.isEmpty() &&
                                      seenCursors->contains(nextCursor);
                seenCursors->insert(nextCursor);
                if (!nextCursor.isEmpty() && !repeated &&
                    *pages < MAX_FOLLOW_PAGES)
                {
                    (*page)(nextCursor);
                }
                else
                {
                    onRows(*rows);
                }
            },
            [onErr](const gql::GqlError &e) {
                if (onErr)
                {
                    onErr(e.message);
                }
            },
            10000);
    };

    (*page)(QString());
}

QDateTime parseFollowedAt(const QString &iso)
{
    const QDateTime dt = QDateTime::fromString(iso, Qt::ISODate);
    return dt.isValid() ? dt.toLocalTime() : QDateTime();
}

QString durationToNow(const QDateTime &followedAt)
{
    if (!followedAt.isValid())
    {
        return QStringLiteral("unknown");
    }
    const auto days = followedAt.daysTo(QDateTime::currentDateTime());
    if (days >= 365)
    {
        return QStringLiteral("%1y %2m").arg(days / 365).arg((days % 365) / 30);
    }
    if (days >= 30)
    {
        return QStringLiteral("%1m %2d").arg(days / 30).arg(days % 30);
    }
    return QStringLiteral("%1d").arg(days);
}

// Shared core: FollowButton_FollowUser for a resolved target id.
void followById(const ChannelPtr &channel, const QString &targetId)
{
    if (targetId.isEmpty() || channel == nullptr)
    {
        return;
    }
    QString err;
    auto token = LimerinoAuth::resolveReadToken(&err);
    if (!token.hasToken())
    {
        say(channel, err.isEmpty()
                         ? LimerinoAuth::errors::tokenRequiredMessage(
                               QStringLiteral("follow a channel"))
                         : err);
        return;
    }

    gql::executePersisted(
        gql::PQ_FOLLOW_USER,
        QJsonObject{{QStringLiteral("input"),
                     QJsonObject{{QStringLiteral("disableNotifications"), false},
                                 {QStringLiteral("targetID"), targetId}}}},
        token.token,
        [weak = std::weak_ptr(channel)](const QJsonObject & /*data*/) {
            if (auto chan = weak.lock())
            {
                // Plugin prints the unconditional success line.
                chan->addSystemMessage(QStringLiteral("Follow request successful!"));
            }
        },
        [weak = std::weak_ptr(channel)](const gql::GqlError &e) {
            if (auto chan = weak.lock())
            {
                chan->addSystemMessage(
                    QStringLiteral("Follow request failed! %1").arg(e.message));
            }
        });
}

}  // namespace

void followChannelFromMenu(ChannelPtr channel)
{
    auto *tchan = dynamic_cast<TwitchChannel *>(channel.get());
    if (tchan == nullptr)
    {
        return;
    }
    followById(channel, tchan->roomId());
}

QString follow(const CommandContext &ctx)
{
    auto *tchan = ctx.twitchChannel;
    if (tchan == nullptr)
    {
        return QStringLiteral("/follow: only available in Twitch channels");
    }
    const ChannelPtr channel = ctx.channel;

    QString target = ctx.words.value(1);
    if (target.isEmpty())
    {
        // Plugin: no channel name -> follow current channel
        followChannelFromMenu(channel);
        return {};
    }
    target.remove(u'@');

    QString err;
    auto token = LimerinoAuth::resolveReadToken(&err);
    if (!token.hasToken())
    {
        say(channel, err.isEmpty()
                         ? LimerinoAuth::errors::tokenRequiredMessage(
                               QStringLiteral("follow a channel"))
                         : err);
        return {};
    }

    gql::executePersisted(
        gql::PQ_GET_USER_ID,
        QJsonObject{{QStringLiteral("login"), target},
                    {QStringLiteral("lookupType"), QStringLiteral("ALL")}},
        token.token,
        [weak = std::weak_ptr(channel), target, token = token.token](
            const QJsonObject &data) {
            const QString id = data[QStringLiteral("user")]
                                   .toObject()[QStringLiteral("id")]
                                   .toString();
            auto chan = weak.lock();
            if (!chan)
            {
                return;
            }
            if (id.isEmpty())
            {
                chan->addSystemMessage(
                    QStringLiteral("Could not find ID for %1").arg(target));
                return;
            }
            followById(chan, id);
        },
        [weak = std::weak_ptr(channel), target](const gql::GqlError &e) {
            if (auto chan = weak.lock())
            {
                chan->addSystemMessage(
                    QStringLiteral("Could not find ID for %1").arg(target));
            }
        });
    return {};
}

void openFollowerListFor(Split *split)
{
    if (split == nullptr)
    {
        return;
    }
    const auto chan = split->getSelectedChannel();
    auto *tchan = dynamic_cast<TwitchChannel *>(chan.get());
    if (tchan == nullptr)
    {
        return;
    }
    const QString login = tchan->getName();

    auto *dialog = new limerino::LimerinoResultDialog;
    dialog->setAttribute(Qt::WA_DeleteOnClose);
    dialog->setWindowTitle(QStringLiteral("Followers - %1").arg(login));
    dialog->resultList()->setTitleText(
        QStringLiteral("People following %1").arg(login));
    dialog->resultList()->setColumns(
        {QStringLiteral("user"), QStringLiteral("following since"),
         QStringLiteral("for")});
    dialog->resultList()->enableSearch(true);
    dialog->resultList()->setStatusText(QStringLiteral("fetching..."));
    dialog->resize(560, 540);
    dialog->show();

    QString err;
    auto token = LimerinoAuth::resolveReadToken(&err);
    if (!token.hasToken())
    {
        dialog->resultList()->setStatusText(
            err.isEmpty() ? LimerinoAuth::errors::tokenRequiredMessage(
                                QStringLiteral("view followers"))
                          : err);
        return;
    }

    // Right-click: Block <user> (resolves the id, then uses the PRIMARY
    // account's /block permission - blocking is a primary-login action).
    dialog->resultList()->setRowMenuProvider(
        [g = QPointer<limerino::LimerinoResultDialog>(dialog),
         token = token.token](const QStringList &row, QMenu *menu) {
            const QString user = row.value(0);
            menu->addAction(QStringLiteral("Block %1").arg(user),
                            [g, user, token]() mutable {
                                gql::executePersisted(
                                    gql::PQ_GET_USER_ID,
                                    QJsonObject{{QStringLiteral("login"), user},
                                                {QStringLiteral("lookupType"),
                                                 QStringLiteral("ALL")}},
                                    token,
                                    [g, user](const QJsonObject &data) {
                                        const QString id =
                                            data[QStringLiteral("user")]
                                                .toObject()[QStringLiteral("id")]
                                                .toString();
                                        if (id.isEmpty())
                                        {
                                            if (g)
                                            {
                                                g->resultList()->setStatusText(
                                                    QStringLiteral("could not resolve %1").arg(user));
                                            }
                                            return;
                                        }
                                        const auto app = getApp();
                                        app->getAccounts()
                                            ->twitch.getCurrent()
                                            ->blockUser(
                                                id, user, g,
                                                [g, user] {
                                                    if (g)
                                                    {
                                                        g->resultList()->setStatusText(
                                                            QStringLiteral("blocked %1").arg(user));
                                                    }
                                                },
                                                [g, user] {
                                                    if (g)
                                                    {
                                                        g->resultList()->setStatusText(
                                                            QStringLiteral("could not block %1").arg(user));
                                                    }
                                                });
                                    },
                                    [g, user](const gql::GqlError &e) {
                                        if (g)
                                        {
                                            g->resultList()->setStatusText(
                                                e.message);
                                        }
                                    });
                            });
        });

    fetchFollows(
        login, true, token.token,
        [g = QPointer<limerino::LimerinoResultDialog>(dialog)](
            const QVector<FollowRow> &rows) {
            if (!g)
            {
                return;
            }
            QVector<QStringList> out;
            out.reserve(rows.size());
            for (const FollowRow &r : rows)
            {
                const QDateTime dt = parseFollowedAt(r.followedAt);
                out.append({r.login,
                            dt.isValid()
                                ? dt.toString(QStringLiteral("yyyy-MM-dd hh:mm"))
                                : QStringLiteral("unknown"),
                            durationToNow(dt)});
            }
            g->resultList()->setRows(out);
            g->resultList()->setStatusText(QStringLiteral("%1 followers")
                                               .arg(rows.size()));
        },
        [g = QPointer<limerino::LimerinoResultDialog>(dialog)](const QString &err) {
            if (g)
            {
                g->resultList()->setStatusText(err);
            }
        },
        [g = QPointer<limerino::LimerinoResultDialog>(dialog)](int pageCount,
                                                              int rowCount) {
            if (g)
            {
                g->resultList()->setStatusText(
                    QStringLiteral("fetching... page %1, %2 rows")
                        .arg(pageCount)
                        .arg(rowCount));
            }
        });
}

void openFollowingListFor(Split *split)
{
    if (split == nullptr)
    {
        return;
    }
    const auto chan = split->getSelectedChannel();
    auto *tchan = dynamic_cast<TwitchChannel *>(chan.get());
    if (tchan == nullptr)
    {
        return;
    }
    const QString login = tchan->getName();

    auto *dialog = new limerino::LimerinoResultDialog;
    dialog->setAttribute(Qt::WA_DeleteOnClose);
    dialog->setWindowTitle(QStringLiteral("Following - %1").arg(login));
    dialog->resultList()->setTitleText(
        QStringLiteral("Channels that %1 follows").arg(login));
    dialog->resultList()->setColumns(
        {QStringLiteral("user"), QStringLiteral("live"),
         QStringLiteral("notifications")});
    dialog->resultList()->setStatusText(QStringLiteral("fetching..."));
    dialog->resize(560, 540);
    dialog->show();

    QString err;
    auto token = LimerinoAuth::resolveReadToken(&err);
    if (!token.hasToken())
    {
        dialog->resultList()->setStatusText(
            err.isEmpty() ? LimerinoAuth::errors::tokenRequiredMessage(
                                QStringLiteral("view following"))
                          : err);
        return;
    }

    // Right-click: Open chat (new tab) / Open channel (browser)
    dialog->resultList()->setRowMenuProvider(
        [](const QStringList &row, QMenu *menu) {
            const QString user = row.value(0);
            menu->addAction(QStringLiteral("Open chat"), [user] {
                auto chan = getApp()->getTwitch()->getOrAddChannel(user);
                auto &nb = getApp()->getWindows()->getMainWindow().getNotebook();
                SplitContainer *container = nb.addPage(true);
                auto *split = new Split(container);
                split->setChannel(chan);
                container->insertSplit(split);
            });
            menu->addAction(QStringLiteral("Open channel in browser"), [user] {
                QDesktopServices::openUrl(QUrl(
                    QStringLiteral("https://www.twitch.tv/%1").arg(user)));
            });
        });

    fetchFollows(
        login, false, token.token,
        [g = QPointer<limerino::LimerinoResultDialog>(dialog)](
            const QVector<FollowRow> &rows) {
            if (!g)
            {
                return;
            }
            QVector<QStringList> out;
            out.reserve(rows.size());
            for (const FollowRow &r : rows)
            {
                out.append({r.login,
                            r.live ? QStringLiteral(u"\u25CF %1 (%2 viewers)")
                                         .arg(r.game)
                                         .arg(r.viewers)
                                   : QString(),
                            r.notificationsOn ? QStringLiteral("ON")
                                              : QStringLiteral("OFF")});
            }
            g->resultList()->setRows(out);
            g->resultList()->setStatusText(
                QStringLiteral("following %1 channels").arg(rows.size()));
        },
        [g = QPointer<limerino::LimerinoResultDialog>(dialog)](const QString &err) {
            if (g)
            {
                g->resultList()->setStatusText(err);
            }
        },
        [g = QPointer<limerino::LimerinoResultDialog>(dialog)](int pageCount,
                                                              int rowCount) {
            if (g)
            {
                g->resultList()->setStatusText(
                    QStringLiteral("fetching... page %1, %2 rows")
                        .arg(pageCount)
                        .arg(rowCount));
            }
        });
}

}  // namespace chatterino::LimerinoCommands
