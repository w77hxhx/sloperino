// SPDX-License-Identifier: MIT

#include "providers/limerino/pubsub/LimerinoChannelNameResolver.hpp"

#include "Application.hpp"
#include "common/network/NetworkRequest.hpp"
#include "common/network/NetworkResult.hpp"
#include "providers/limerino/LimerinoAuth.hpp"
#include "providers/limerino/LimerinoRateLimiter.hpp"
#include "providers/twitch/TwitchChannel.hpp"
#include "providers/twitch/TwitchIrcServer.hpp"

#include <QHash>
#include <QJsonArray>
#include <QJsonObject>
#include <QTimer>
#include <QUrl>
#include <QUrlQuery>

namespace chatterino::limerino {

namespace {

constexpr int COALESCE_MS = 100;
constexpr int HELIX_TIMEOUT_MS = 10000;
const QString HELIX_USERS_URL =
    QStringLiteral("https://api.twitch.tv/helix/users");
const QString RATE_BUCKET = QStringLiteral("api.twitch.tv/users");

struct Pending {
    QList<ChannelNameResolvedCallback> callbacks;
};

struct State {
    QHash<QString, QString> cache;  // id -> login (or displayName)
    QHash<QString, Pending> pending;
    QStringList queuedIds;
    QTimer coalesce;
    bool requestInFlight = false;
};

void flushQueued();

State &state()
{
    // Heap singleton: State holds a QTimer (non-copyable), so it cannot be
    // returned from an immediately-invoked initializer by value (clang-cl).
    static State *s = [] {
        auto *out = new State;
        out->coalesce.setSingleShot(true);
        out->coalesce.setInterval(COALESCE_MS);
        QObject::connect(&out->coalesce, &QTimer::timeout, [] {
            flushQueued();
        });
        return out;
    }();
    return *s;
}

QString nameFromOpenChannel(const QString &id)
{
    auto *app = tryGetApp();
    if (app == nullptr || app->getTwitch() == nullptr)
    {
        return {};
    }
    auto channelPtr = app->getTwitch()->getChannelOrEmptyByID(id);
    if (channelPtr->isEmpty())
    {
        return {};
    }
    auto *tchan = dynamic_cast<TwitchChannel *>(channelPtr.get());
    if (tchan == nullptr)
    {
        return {};
    }
    return tchan->getName();
}

QString nameFromModeratedLists(const QString &id)
{
    for (const auto &account : LimerinoAuth::accounts())
    {
        for (const auto &ch : account.moderatedChannels)
        {
            if (ch.id == id)
            {
                if (!ch.login.isEmpty())
                {
                    return ch.login;
                }
                if (!ch.displayName.isEmpty())
                {
                    return ch.displayName;
                }
            }
        }
        if (account.userId == id)
        {
            if (!account.login.isEmpty())
            {
                return account.login;
            }
            if (!account.displayName.isEmpty())
            {
                return account.displayName;
            }
        }
    }
    return {};
}

void finishId(const QString &id, const QString &name)
{
    auto &s = state();
    s.cache.insert(id, name);
    const auto pending = s.pending.take(id);
    for (const auto &cb : pending.callbacks)
    {
        if (cb)
        {
            cb(name);
        }
    }
}

void startHelix(const QStringList &ids)
{
    if (ids.isEmpty())
    {
        return;
    }

    auto &s = state();
    s.requestInFlight = true;

    QString resolveErr;
    auto token = LimerinoAuth::resolveReadToken(&resolveErr);
    if (!token.hasToken())
    {
        for (const auto &id : ids)
        {
            finishId(id, id);
        }
        s.requestInFlight = false;
        if (!s.queuedIds.isEmpty())
        {
            s.coalesce.start();
        }
        return;
    }

    const QString oauth = token.token;
    LimerinoRateLimiter::instance().execute(
        RATE_BUCKET,
        [ids, oauth] {
            QUrlQuery query;
            for (const auto &id : ids)
            {
                query.addQueryItem(QStringLiteral("id"), id);
            }
            QUrl url(HELIX_USERS_URL);
            url.setQuery(query);
            return NetworkRequest(url, NetworkRequestType::Get)
                .header("Client-Id", LimerinoAuth::CLIENT_ID)
                .header("Authorization", QStringLiteral("Bearer ") + oauth)
                .timeout(HELIX_TIMEOUT_MS);
        },
        [ids](NetworkResult result) {
            auto &s = state();
            s.requestInFlight = false;

            QHash<QString, QString> got;
            const QJsonArray data =
                result.parseJson().value(QStringLiteral("data")).toArray();
            for (int i = 0; i < data.size(); ++i)
            {
                const QJsonObject user = data.at(i).toObject();
                const QString id = user.value(QStringLiteral("id")).toString();
                const QString login =
                    user.value(QStringLiteral("login")).toString();
                const QString display =
                    user.value(QStringLiteral("display_name")).toString();
                const QString name =
                    !login.isEmpty()
                        ? login
                        : (!display.isEmpty() ? display : id);
                if (!id.isEmpty())
                {
                    got.insert(id, name);
                }
            }
            for (const auto &id : ids)
            {
                finishId(id, got.value(id, id));
            }
            if (!s.queuedIds.isEmpty() && !s.coalesce.isActive())
            {
                s.coalesce.start();
            }
        },
        [ids](NetworkResult /*result*/) {
            auto &s = state();
            s.requestInFlight = false;
            for (const auto &id : ids)
            {
                finishId(id, id);
            }
            if (!s.queuedIds.isEmpty() && !s.coalesce.isActive())
            {
                s.coalesce.start();
            }
        });
}

void flushQueued()
{
    auto &s = state();
    if (s.requestInFlight || s.queuedIds.isEmpty())
    {
        return;
    }
    // Helix allows up to 100 ids per request.
    QStringList batch = s.queuedIds.mid(0, 100);
    s.queuedIds = s.queuedIds.mid(batch.size());
    startHelix(batch);
}

}  // namespace

bool trySyncChannelName(const QString &id, QString &outName)
{
    if (id.isEmpty())
    {
        return false;
    }

    auto &s = state();
    auto it = s.cache.constFind(id);
    if (it != s.cache.cend())
    {
        outName = it.value();
        return true;
    }

    const QString open = nameFromOpenChannel(id);
    if (!open.isEmpty())
    {
        s.cache.insert(id, open);
        outName = open;
        return true;
    }

    const QString moderated = nameFromModeratedLists(id);
    if (!moderated.isEmpty())
    {
        s.cache.insert(id, moderated);
        outName = moderated;
        return true;
    }

    return false;
}

void resolveChannelName(const QString &id, ChannelNameResolvedCallback cb)
{
    if (!cb)
    {
        return;
    }
    if (id.isEmpty())
    {
        cb(id);
        return;
    }

    QString syncName;
    if (trySyncChannelName(id, syncName))
    {
        cb(syncName);
        return;
    }

    // Headless tests / early startup: never block event emission on Helix.
    if (tryGetApp() == nullptr)
    {
        cb(id);
        return;
    }

    QString resolveErr;
    auto token = LimerinoAuth::resolveReadToken(&resolveErr);
    if (!token.hasToken())
    {
        cb(id);
        return;
    }

    auto &s = state();
    auto &pending = s.pending[id];
    pending.callbacks.append(std::move(cb));
    if (pending.callbacks.size() == 1)
    {
        s.queuedIds.append(id);
        if (!s.requestInFlight && !s.coalesce.isActive())
        {
            s.coalesce.start();
        }
    }
}

}  // namespace chatterino::limerino
