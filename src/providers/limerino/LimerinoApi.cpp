// SPDX-License-Identifier: MIT

#include "providers/limerino/LimerinoApi.hpp"

#include "Application.hpp"
#include "common/network/NetworkRequest.hpp"
#include "common/network/NetworkResult.hpp"
#include "controllers/accounts/AccountController.hpp"
#include "providers/limerino/LimerinoErrors.hpp"
#include "providers/twitch/TwitchAccount.hpp"
#include "providers/twitch/TwitchAccountManager.hpp"
#include "singletons/Settings.hpp"

#include "common/network/NetworkRequest.hpp"
#include "common/network/NetworkResult.hpp"
#include "providers/limerino/LimerinoRateLimiter.hpp"

#include <QJsonArray>
#include <QJsonDocument>
#include <QJsonObject>
#include <QUrl>
#include <QUrlQuery>

namespace chatterino::LimerinoApi {

namespace {

const QString HELIX_BASE = QStringLiteral("https://api.twitch.tv/helix/");
const QString VALIDATE_URL =
    QStringLiteral("https://id.twitch.tv/oauth2/validate");

QString primaryToken()
{
    const auto user = getApp()->getAccounts()->twitch.getCurrent();
    if (!user || user->isAnon())
    {
        return {};
    }
    return user->getOAuthToken();
}

// One-shot scope cache for the primary token's /validate response.
QStringList &primaryScopeCache()
{
    static QStringList scopes;
    return scopes;
}

bool &primaryScopesLoaded()
{
    static bool loaded = false;
    return loaded;
}

}  // namespace

void helixTokenFor(
    const QString &requiredScope,
    const std::function<void(const LimerinoAuth::LimerinoAuthToken &token,
                             bool fromPrimary)> &cb,
    const std::function<void(const QString &err)> &onError)
{
    const auto decide = [requiredScope, cb, onError]() {
        const QString pt = primaryToken();
        if (!pt.isEmpty() && primaryScopesLoaded() &&
            primaryScopeCache().contains(requiredScope))
        {
            cb(LimerinoAuth::LimerinoAuthToken{pt, {}, {}}, true);
            return;
        }

        // Fall back to the Limerino resolver token.
        QString err;
        auto token = LimerinoAuth::resolveCurrentUserToken(&err);
        if (!token.hasToken())
        {
            onError(err.isEmpty() ? LimerinoAuth::errors::tokenRequiredMessage(
                                        QStringLiteral("make this Helix call"))
                                  : err);
            return;
        }
        cb(token, false);
    };

    if (primaryScopesLoaded() || primaryToken().isEmpty())
    {
        decide();
        return;
    }

    NetworkRequest(QUrl(VALIDATE_URL), NetworkRequestType::Get)
        .header("Authorization", QStringLiteral("OAuth ") + primaryToken())
        .hideRequestBody()
        .timeout(10000)
        .onSuccess([decide](NetworkResult result) {
            const QJsonObject o = result.parseJson();
            for (const QJsonValue &v : o[QStringLiteral("scopes")].toArray())
            {
                primaryScopeCache().append(v.toString());
            }
            primaryScopesLoaded() = true;
            decide();
        })
        .onError([decide](NetworkResult /*result*/) {
            primaryScopesLoaded() = true;  // don't retry hammering
            decide();
        })
        .execute();
}

void sendChatMessage(
    const QString &token, const QString &broadcasterId, const QString &senderId,
    const QString &message, const std::function<void()> &onSent,
    const std::function<void(const QString &dropReason)> &onDrop)
{
    const QJsonObject body{
        {QStringLiteral("broadcaster_id"), broadcasterId},
        {QStringLiteral("sender_id"), senderId},
        {QStringLiteral("message"), message},
    };
    NetworkRequest(QUrl(HELIX_BASE + QStringLiteral("chat/messages")),
                   NetworkRequestType::Post)
        .header("Content-Type", "application/json")
        .header("Authorization", QStringLiteral("Bearer ") + token)
        .header("Client-Id", LimerinoAuth::CLIENT_ID)
        .hideRequestBody()
        .json(body)
        .timeout(5000)
        .onSuccess([onSent, onDrop](NetworkResult result) {
            const QJsonArray data =
                result.parseJson()[QStringLiteral("data")].toArray();
            if (data.isEmpty())
            {
                if (onDrop)
                {
                    onDrop(QStringLiteral("empty response"));
                }
                return;
            }
            const QJsonObject status = data.first().toObject();
            if (status[QStringLiteral("is_sent")].toBool())
            {
                if (onSent)
                {
                    onSent();
                }
                return;
            }
            QString reason = status[QStringLiteral("drop_reason")]
                                 .toObject()[QStringLiteral("message")]
                                 .toString();
            if (reason.isEmpty())
            {
                reason = QStringLiteral("message dropped (no reason given)");
            }
            if (onDrop)
            {
                onDrop(reason);
            }
        })
        .onError([onDrop](NetworkResult result) {
            if (onDrop)
            {
                onDrop(LimerinoAuth::errors::describeHttpFailure(
                    result.status().value_or(0), QStringLiteral("send message")));
            }
        })
        .execute();
}

QString pasteHost()
{
    QString host = getSettings()->limerinoPasteHost.getValue();
    while (host.endsWith('/'))
    {
        host.chop(1);
    }
    return host;
}

void uploadPaste(const QString &text,
                 const std::function<void(const QString &url)> &onSuccess,
                 const ApiErrorCallback &onError)
{
    NetworkRequest(QUrl(pasteHost() + QStringLiteral("/documents")),
                   NetworkRequestType::Post)
        .payload(text.toUtf8())
        .timeout(50000)
        .onSuccess([onSuccess, onError](NetworkResult result) {
            const QString key = QJsonDocument::fromJson(result.getData())
                                    .object()[QStringLiteral("key")]
                                    .toString();
            if (key.isEmpty())
            {
                if (onError)
                {
                    onError(QStringLiteral("no key in paste response"));
                }
                return;
            }
            if (onSuccess)
            {
                onSuccess(pasteHost() + QStringLiteral("/raw/") + key);
            }
        })
        .onError([onError](NetworkResult result) {
            if (onError)
            {
                onError(LimerinoAuth::errors::describeHttpFailure(
                    result.status().value_or(0), QStringLiteral("upload paste")));
            }
        })
        .execute();
}

// --- moderation (batch N3) ----------------------------------------------------
// Moderation endpoints accept the *primary* chatterino login's client id and
// OAuth token (same as every built-in /ban). The Helix request is built inside
// this translation unit; no raw NetworkRequest appears outside of LimerinoApi.

namespace {

NetworkRequest moderationRequest(NetworkRequestType type, const QString &path,
                                 const QUrlQuery &query,
                                 const QString &primaryToken,
                                 const QString &primaryClientId,
                                 const QJsonObject *payload)
{
    QUrl url(HELIX_BASE + path);
    url.setQuery(query);

    auto req = NetworkRequest(url, type)
                   .timeout(5000)
                   .header("Accept", "application/json")
                   .header("Client-Id", primaryClientId)
                   .header("Authorization",
                           QStringLiteral("Bearer ") + primaryToken);
    if (payload != nullptr)
    {
        req = std::move(req)
                  .header("Content-Type", "application/json")
                  .json(*payload);
    }
    return req;
}

QString moderationErrorMessage(const QString &action, const NetworkResult &result)
{
    const QString body = QString::fromUtf8(result.getData());
    const auto obj = QJsonDocument::fromJson(body.toUtf8()).object();
    QString msg = obj[QStringLiteral("message")].toString();
    if (msg.isEmpty())
    {
        msg = LimerinoAuth::errors::describeHttpFailure(
            result.status().value_or(0), action);
    }
    return msg;
}

}  // namespace

void banUser(const QString &broadcasterID, const QString &moderatorID,
             const QString &userID, std::optional<int> durationSeconds,
             const QString &reason, const QString &bucketKey,
             std::function<void()> onSuccess,
             std::function<void(QString)> onError)
{
    QUrlQuery query;
    query.addQueryItem(QStringLiteral("broadcaster_id"), broadcasterID);
    query.addQueryItem(QStringLiteral("moderator_id"), moderatorID);

    QJsonObject data;
    data[QStringLiteral("user_id")] = userID;
    data[QStringLiteral("reason")] = reason;
    if (durationSeconds.has_value())
    {
        data[QStringLiteral("duration")] = *durationSeconds;
    }
    QJsonObject body;
    body[QStringLiteral("data")] = data;

    const QString token = primaryToken();
    const QString clientId =
        getApp()->getAccounts()->twitch.getCurrent()->getOAuthClient();

    LimerinoRateLimiter::instance().execute(
        bucketKey,
        [=] {
            return moderationRequest(NetworkRequestType::Post,
                                     QStringLiteral("moderation/bans"), query,
                                     token, clientId, &body);
        },
        [onSuccess](NetworkResult /*result*/) {
            if (onSuccess)
            {
                onSuccess();
            }
        },
        [onError](NetworkResult result) {
            if (onError)
            {
                onError(moderationErrorMessage(QStringLiteral("ban / timeout"),
                                               result));
            }
        });
}

void warnUser(const QString &broadcasterID, const QString &moderatorID,
              const QString &userID, const QString &reason,
              const QString &bucketKey, std::function<void()> onSuccess,
              std::function<void(QString)> onError)
{
    QUrlQuery query;
    query.addQueryItem(QStringLiteral("broadcaster_id"), broadcasterID);
    query.addQueryItem(QStringLiteral("moderator_id"), moderatorID);

    QJsonObject body;
    body[QStringLiteral("data")] = QJsonObject{
        {QStringLiteral("user_id"), userID},
        {QStringLiteral("reason"), reason},
    };

    const QString token = primaryToken();
    const QString clientId =
        getApp()->getAccounts()->twitch.getCurrent()->getOAuthClient();

    LimerinoRateLimiter::instance().execute(
        bucketKey,
        [=] {
            return moderationRequest(NetworkRequestType::Post,
                                     QStringLiteral("moderation/warnings"),
                                     query, token, clientId, &body);
        },
        [onSuccess](NetworkResult /*result*/) {
            if (onSuccess)
            {
                onSuccess();
            }
        },
        [onError](NetworkResult result) {
            if (onError)
            {
                onError(moderationErrorMessage(QStringLiteral("warn"), result));
            }
        });
}

void deleteChatMessage(const QString &broadcasterID,
                       const QString &moderatorID, const QString &messageID,
                       const QString &bucketKey, std::function<void()> onSuccess,
                       std::function<void(QString)> onError)
{
    QUrlQuery query;
    query.addQueryItem(QStringLiteral("broadcaster_id"), broadcasterID);
    query.addQueryItem(QStringLiteral("moderator_id"), moderatorID);
    if (!messageID.isEmpty())
    {
        query.addQueryItem(QStringLiteral("message_id"), messageID);
    }

    const QString token = primaryToken();
    const QString clientId =
        getApp()->getAccounts()->twitch.getCurrent()->getOAuthClient();

    LimerinoRateLimiter::instance().execute(
        bucketKey,
        [=] {
            return moderationRequest(NetworkRequestType::Delete,
                                     QStringLiteral("moderation/chat"), query,
                                     token, clientId, nullptr);
        },
        [onSuccess](NetworkResult /*result*/) {
            if (onSuccess)
            {
                onSuccess();
            }
        },
        [onError](NetworkResult result) {
            if (onError)
            {
                onError(moderationErrorMessage(QStringLiteral("delete message"),
                                               result));
            }
        });
}

void unbanUser(const QString &broadcasterID, const QString &moderatorID,
               const QString &userID, const QString &bucketKey,
               std::function<void()> onSuccess,
               std::function<void(QString)> onError)
{
    QUrlQuery query;
    query.addQueryItem(QStringLiteral("broadcaster_id"), broadcasterID);
    query.addQueryItem(QStringLiteral("moderator_id"), moderatorID);
    query.addQueryItem(QStringLiteral("user_id"), userID);

    const QString token = primaryToken();
    const QString clientId =
        getApp()->getAccounts()->twitch.getCurrent()->getOAuthClient();

    LimerinoRateLimiter::instance().execute(
        bucketKey,
        [=] {
            return moderationRequest(NetworkRequestType::Delete,
                                     QStringLiteral("moderation/bans"), query,
                                     token, clientId, nullptr);
        },
        [onSuccess](NetworkResult /*result*/) {
            if (onSuccess)
            {
                onSuccess();
            }
        },
        [onError](NetworkResult result) {
            if (onError)
            {
                onError(moderationErrorMessage(QStringLiteral("unban/untimeout"),
                                               result));
            }
        });
}

}  // namespace chatterino::LimerinoApi
