// SPDX-License-Identifier: MIT
// Secondary "Limerino Extra Features" auth.
// Completely separate store from the primary Twitch login (/accounts/uid<id>/).
// See FORK.md.

#include "providers/limerino/LimerinoAuth.hpp"

#include "Application.hpp"
#include "common/network/NetworkCommon.hpp"
#include "common/network/NetworkRequest.hpp"
#include "common/network/NetworkResult.hpp"
#include "controllers/accounts/AccountController.hpp"
#include "providers/twitch/TwitchAccount.hpp"
#include "providers/twitch/TwitchAccountManager.hpp"
#include "singletons/Settings.hpp"

#include <QJsonArray>
#include <QJsonDocument>
#include <QJsonObject>
#include <QPointer>
#include <QTimer>
#include <QUrl>
#include <QUrlQuery>

#include <algorithm>
#include <memory>

namespace chatterino::LimerinoAuth {

// Twitch web / front-end first-party client. Not the Android-TV id the
// original auth artifact used. This is the only provider of the value;
// everything else (Helix, GQL, Hermes WS URL, device flow) reads it from here.
// Supported grants: OAuth 2.0 Device Authorization Grant (twitch.tv/activate)
// and the refresh_token grant below; both public, no secret.
const QString CLIENT_ID =
    QStringLiteral("kd1unb4b3q4t58fwlpcbzcbnm76a8fp");
// NOTE: the device flow deliberately requests this broader scope list so the
// Helix fallback path (chat/messages, moderation/channels) is authorized too.
// Scopes are unchanged from the prior client - they are all public OAuth
// scopes, none client-specific.
const QString CLIENT_SCOPES = QStringLiteral(
    "chat:read chat:edit channel:moderate channel:manage:predictions "
    "channel:read:redemptions channel:manage:redemptions "
    "moderator:manage:announcements moderator:manage:chat_messages "
    "moderator:manage:chat_settings moderator:read:chat_settings "
    "moderator:read:followers user:write:chat user:read:moderated_channels");
const QString AUTH_DEVICE_URL =
    QStringLiteral("https://id.twitch.tv/oauth2/device");
const QString AUTH_TOKEN_URL =
    QStringLiteral("https://id.twitch.tv/oauth2/token");

namespace {

const QString AUTH_VALIDATE_URL =
    QStringLiteral("https://id.twitch.tv/oauth2/validate");
const QString HELIX_USERS_URL =
    QStringLiteral("https://api.twitch.tv/helix/users");
const QString HELIX_MODERATED_CHANNELS_URL =
    QStringLiteral("https://api.twitch.tv/helix/moderation/channels");

constexpr int REQUEST_TIMEOUT_MS = 15000;

// RULE: never log a token. Use this on every externally-sourced error text.
QString redact(QString text, const QString &token)
{
    if (!token.isEmpty() && text.contains(token))
    {
        text.replace(token, QStringLiteral("<redacted>"));
    }
    return text;
}

struct Store {
    QVector<LimerinoAuthAccount> items;
    bool loaded = false;
};

Store &store()
{
    static Store s;
    return s;
}

void sortItems(QVector<LimerinoAuthAccount> &items)
{
    std::sort(items.begin(), items.end(),
              [](const LimerinoAuthAccount &a, const LimerinoAuthAccount &b) {
                  const int cmp = QString::localeAwareCompare(a.login, b.login);
                  if (cmp != 0)
                  {
                      return cmp < 0;
                  }
                  return a.userId < b.userId;
              });
}

LimerinoAuthAccount accountFromJson(const QJsonObject &o)
{
    LimerinoAuthAccount a;
    a.userId = o[QStringLiteral("userId")].toString();
    a.login = o[QStringLiteral("login")].toString();
    a.displayName = o[QStringLiteral("displayName")].toString();
    a.token = o[QStringLiteral("token")].toString();
    a.valid = o[QStringLiteral("valid")].toBool(false);
    a.lastError = o[QStringLiteral("lastError")].toString();
    a.lastValidatedAt = QDateTime::fromString(
        o[QStringLiteral("lastValidatedAt")].toString(), Qt::ISODate);
    for (const QJsonValue &v : o[QStringLiteral("scopes")].toArray())
    {
        a.scopes.append(v.toString());
    }
    a.refreshToken = o[QStringLiteral("refreshToken")].toString();
    a.expiresAt = QDateTime::fromString(
        o[QStringLiteral("expiresAt")].toString(), Qt::ISODate);
    const QJsonArray channels =
        o[QStringLiteral("moderatedChannels")].toArray();
    for (const QJsonValue &v : channels)
    {
        const QJsonObject c = v.toObject();
        a.moderatedChannels.append(LimerinoAuthChannel{
            c[QStringLiteral("id")].toString(),
            c[QStringLiteral("login")].toString(),
            c[QStringLiteral("displayName")].toString()});
    }
    return a;
}

QJsonObject accountToJson(const LimerinoAuthAccount &a)
{
    QJsonArray channels;
    for (const LimerinoAuthChannel &c : a.moderatedChannels)
    {
        channels.append(QJsonObject{
            {QStringLiteral("id"), c.id},
            {QStringLiteral("login"), c.login},
            {QStringLiteral("displayName"), c.displayName},
        });
    }
    return QJsonObject{
        {QStringLiteral("userId"), a.userId},
        {QStringLiteral("login"), a.login},
        {QStringLiteral("displayName"), a.displayName},
        {QStringLiteral("token"), a.token},
        {QStringLiteral("valid"), a.valid},
        {QStringLiteral("lastError"), a.lastError},
        {QStringLiteral("lastValidatedAt"),
         a.lastValidatedAt.toString(Qt::ISODate)},
        {QStringLiteral("scopes"), QJsonArray::fromStringList(a.scopes)},
        {QStringLiteral("refreshToken"), a.refreshToken},
        {QStringLiteral("expiresAt"), a.expiresAt.toString(Qt::ISODate)},
        {QStringLiteral("moderatedChannels"), channels},
    };
}

void ensureLoaded()
{
    Store &s = store();
    if (s.loaded)
    {
        return;
    }
    s.loaded = true;
    s.items.clear();

    QJsonParseError err{};
    const QJsonDocument doc = QJsonDocument::fromJson(
        getSettings()->limerinoAuthAccounts.getValue().toUtf8(), &err);
    if (err.error != QJsonParseError::NoError || !doc.isArray())
    {
        return;
    }
    for (const QJsonValue &v : doc.array())
    {
        LimerinoAuthAccount a = accountFromJson(v.toObject());
        if (!a.token.isEmpty())
        {
            s.items.append(std::move(a));
        }
    }
    sortItems(s.items);
}

// Every store mutation goes through here: persist + notify.
void save()
{
    Store &s = store();
    QJsonArray arr;
    for (const LimerinoAuthAccount &a : s.items)
    {
        arr.append(accountToJson(a));
    }
    getSettings()->limerinoAuthAccounts.setValue(QString::fromUtf8(
        QJsonDocument(arr).toJson(QJsonDocument::Compact)));
    accountsChanged.invoke();
}

// Merge a freshly validated account over a stored one, preserving the device
// grant's refresh token whenever the validate path (which never returns one)
// would otherwise wipe it.
void mergeResolved(LimerinoAuthAccount &dst, const LimerinoAuthAccount &resolved)
{
    const QString priorRefreshToken = dst.refreshToken;
    dst = resolved;
    if (dst.refreshToken.isEmpty())
    {
        dst.refreshToken = priorRefreshToken;
    }
}

}  // namespace

pajlada::Signals::NoArgSignal accountsChanged;

QString normalizeToken(QString raw)
{
    static const QStringList prefixes = {
        QStringLiteral("Authorization:"),
        QStringLiteral("OAuth "),
        QStringLiteral("Bearer "),
        QStringLiteral("oauth:"),
    };

    for (;;)
    {
        QString before = raw;
        raw = raw.trimmed();

        if (raw.size() >= 2 &&
            ((raw.startsWith(u'"') && raw.endsWith(u'"')) ||
             (raw.startsWith(u'\'') && raw.endsWith(u'\''))))
        {
            raw = raw.mid(1, raw.size() - 2).trimmed();
        }

        for (const QString &prefix : prefixes)
        {
            if (raw.startsWith(prefix, Qt::CaseInsensitive))
            {
                raw = raw.mid(prefix.size()).trimmed();
            }
        }

        if (raw == before)
        {
            return raw;
        }
    }
}

QVector<LimerinoAuthAccount> accounts()
{
    ensureLoaded();
    return store().items;
}

LimerinoAuthSummary summary()
{
    ensureLoaded();
    LimerinoAuthSummary out;
    for (const LimerinoAuthAccount &a : store().items)
    {
        ++out.accountCount;
        if (a.valid)
        {
            ++out.validAccountCount;
        }
        else
        {
            ++out.invalidAccountCount;
        }
        out.moderatedChannelCount += a.moderatedChannels.size();
    }
    return out;
}

void resolveToken(const QString &normalizedToken,
                  const std::function<void(const LimerinoAuthAccount &)> &onDone)
{
    auto account = std::make_shared<LimerinoAuthAccount>();
    account->token = normalizedToken;

    const auto failed = [account, onDone](QString reason) {
        account->valid = false;
        account->lastError = redact(std::move(reason), account->token);
        onDone(*account);
    };

    auto fetchModeratedChannels =
        [account, onDone, failed](const QString &normalizedToken) {
            // The broadcaster can always moderate their own channel; start from that.
            account->moderatedChannels = {LimerinoAuthChannel{
                account->userId, account->login, account->displayName}};

            // Only enumerate channels when the token carries the scope;
            // otherwise the own-channel list above is the honest answer.
            if (!account->scopes.contains(
                    QStringLiteral("user:read:moderated_channels")))
            {
                onDone(*account);
                return;
            }
            auto page = std::make_shared<std::function<void(const QString &)>>();
            auto pages = std::make_shared<int>(0);
            *page = [account, onDone, normalizedToken, page,
                     pages](const QString &cursor) {
                QString url = QStringLiteral("%1?user_id=%2&first=100")
                                  .arg(HELIX_MODERATED_CHANNELS_URL,
                                       account->userId);
                if (!cursor.isEmpty())
                {
                    url += QStringLiteral("&after=%1").arg(QString::fromUtf8(
                        QUrl::toPercentEncoding(cursor)));
                }
                NetworkRequest(QUrl(url), NetworkRequestType::Get)
                    .header("Client-Id", CLIENT_ID)
                    .header("Authorization",
                            QStringLiteral("Bearer ") + normalizedToken)
                    .hideRequestBody()
                    .timeout(REQUEST_TIMEOUT_MS)
                    .onSuccess(
                        [account, onDone, normalizedToken, page,
                         pages](NetworkResult result) {
                            const QJsonObject body = result.parseJson();
                            const QJsonArray data =
                                body[QStringLiteral("data")].toArray();
                            for (const QJsonValue &v : data)
                            {
                                const QJsonObject c = v.toObject();
                                const QString id =
                                    c[QStringLiteral("broadcaster_id")]
                                        .toString();
                                if (id.isEmpty() || id == account->userId)
                                {
                                    continue;
                                }
                                account->moderatedChannels.append(
                                    LimerinoAuthChannel{
                                        id,
                                        c[QStringLiteral("broadcaster_login")]
                                            .toString(),
                                        c[QStringLiteral("broadcaster_name")]
                                            .toString()});
                            }

                            const QString nextCursor =
                                body[QStringLiteral("pagination")]
                                    .toObject()[QStringLiteral("cursor")]
                                    .toString();
                            // Hard cap: 50 pages (5000 channels) as a runaway guard.
                            if (!nextCursor.isEmpty() && ++(*pages) < 50)
                            {
                                (*page)(nextCursor);
                            }
                            else
                            {
                                onDone(*account);
                            }
                        })
                    .onError(
                        [account, onDone](NetworkResult /*result*/) {
                            // tolerated: keep the own-channel list
                            onDone(*account);
                        })
                    .execute();
            };
            (*page)(QString());
        };

    auto fetchDisplayName = [account, onDone,
                             fetchModeratedChannels](const QString &login,
                                                     const QString &userId,
                                                     const QString &normalizedToken) {
        account->userId = userId;
        account->login = login;
        account->displayName = login;

        NetworkRequest(HELIX_USERS_URL, NetworkRequestType::Get)
            .header("Client-Id", CLIENT_ID)
            .header("Authorization",
                    QStringLiteral("Bearer ") + normalizedToken)
            .hideRequestBody()
            .timeout(REQUEST_TIMEOUT_MS)
            .onSuccess(
                [account, normalizedToken, fetchModeratedChannels,
                 onDone](NetworkResult result) {
                    const QJsonArray data =
                        result.parseJson()[QStringLiteral("data")].toArray();
                    if (!data.isEmpty())
                    {
                        const QJsonObject user = data.first().toObject();
                        const QString dn =
                            user[QStringLiteral("display_name")].toString();
                        const QString lg =
                            user[QStringLiteral("login")].toString();
                        if (!lg.isEmpty())
                        {
                            account->login = lg;
                        }
                        account->displayName = dn.isEmpty() ? account->login : dn;
                    }
                    fetchModeratedChannels(normalizedToken);
                })
            .onError(
                [account, normalizedToken, fetchModeratedChannels,
                 onDone](NetworkResult /*result*/) {
                    // Non-fatal: identity from /validate is already enough.
                    fetchModeratedChannels(normalizedToken);
                })
            .execute();
    };

    NetworkRequest(QUrl(AUTH_VALIDATE_URL), NetworkRequestType::Get)
        .header("Client-Id", CLIENT_ID)
        .header("Authorization", QStringLiteral("OAuth ") + normalizedToken)
        .hideRequestBody()
        .timeout(REQUEST_TIMEOUT_MS)
        .onSuccess(
            [account, normalizedToken, fetchDisplayName,
             onDone](NetworkResult result) {
                const QJsonObject o = result.parseJson();
                const QString login = o[QStringLiteral("login")].toString();
                const QString userId = o[QStringLiteral("user_id")].toString();
                if (userId.isEmpty() || login.isEmpty())
                {
                    account->valid = false;
                    account->lastError = QStringLiteral(
                        "validation response missing login/user_id");
                    onDone(*account);
                    return;
                }
                account->valid = true;
                account->lastError.clear();
                account->lastValidatedAt = QDateTime::currentDateTime();
                account->scopes.clear();
                for (const QJsonValue &v :
                     o[QStringLiteral("scopes")].toArray())
                {
                    account->scopes.append(v.toString());
                }
                const long expiresIn =
                    o[QStringLiteral("expires_in")].toInt(0);
                if (expiresIn > 0)
                {
                    account->expiresAt =
                        QDateTime::currentDateTime().addSecs(expiresIn);
                }
                fetchDisplayName(login, userId, normalizedToken);
            })
        .onError(
            [account, onDone](NetworkResult result) {
                QString reason;
                if (result.status() && *result.status() == 401)
                {
                    reason = QStringLiteral(
                        "token rejected (401): expired, revoked, or issued for "
                        "a different client");
                }
                else
                {
                    reason = QStringLiteral("validation request failed: ") +
                             result.formatError();
                }
                account->valid = false;
                account->lastError = redact(reason, account->token);
                onDone(*account);
            })
        .execute();
}

void addOrUpdateToken(const LimerinoAuthToken &token)
{
    LimerinoAuthToken t = token;
    t.token = normalizeToken(t.token);
    if (t.token.isEmpty())
    {
        return;
    }

    ensureLoaded();
    {
        auto &items = store().items;
        auto it = std::find_if(
            items.begin(), items.end(), [&](const LimerinoAuthAccount &a) {
                return (!t.userId.isEmpty() && !a.userId.isEmpty() &&
                        a.userId == t.userId) ||
                       a.token == t.token;
            });
        if (it == items.end())
        {
            LimerinoAuthAccount a;
            a.token = t.token;
            a.userId = t.userId;
            a.login = t.login;
            a.displayName = t.login;
            a.refreshToken = t.refreshToken;
            if (t.expiresInSec > 0)
            {
                a.expiresAt =
                    QDateTime::currentDateTime().addSecs(t.expiresInSec);
            }
            a.lastError = QStringLiteral("validating...");
            items.append(std::move(a));
        }
        else
        {
            it->token = t.token;
            if (!t.refreshToken.isEmpty())
            {
                it->refreshToken = t.refreshToken;
            }
            if (t.expiresInSec > 0)
            {
                it->expiresAt =
                    QDateTime::currentDateTime().addSecs(t.expiresInSec);
            }
            it->lastError = QStringLiteral("validating...");
        }
        sortItems(items);
    }
    save();

    resolveToken(t.token,
                 [token = t.token](const LimerinoAuthAccount &resolved) {
                     auto &items = store().items;
                     auto it = std::find_if(items.begin(), items.end(),
                                            [&](const LimerinoAuthAccount &a) {
                                                return (!resolved.userId.isEmpty() &&
                                                        a.userId ==
                                                            resolved.userId) ||
                                                       a.token == token;
                                            });
                      if (it == items.end())
                    {
                        items.append(resolved);
                    }
                    else
                    {
                        mergeResolved(*it, resolved);
                    }
                    sortItems(items);
                    save();
                });
}

void removeAccount(const QString &userId)
{
    ensureLoaded();
    auto &items = store().items;
    const auto it = std::remove_if(items.begin(), items.end(),
                                   [&](const LimerinoAuthAccount &a) {
                                       return a.userId == userId;
                                   });
    if (it != items.end())
    {
        items.erase(it, items.end());
        save();
    }
}

void refreshAccounts(
    const std::function<void(const LimerinoAuthRefreshResult &)> &done)
{
    ensureLoaded();
    auto result = std::make_shared<LimerinoAuthRefreshResult>();
    result->total = store().items.size();

    if (result->total == 0)
    {
        if (done)
        {
            done(*result);
        }
        return;
    }

    auto pending = std::make_shared<int>(result->total);
    const QVector<LimerinoAuthAccount> snapshot = store().items;
    for (const LimerinoAuthAccount &old : snapshot)
    {
        // Resolve (validate) a given token for this account and merge the result.
        auto resolveWith = [old, pending, result, done](const QString &token) {
            resolveToken(token,
                         [pending, result, old, done](
                             const LimerinoAuthAccount &resolved) {
                if (resolved.valid)
                {
                    ++result->valid;
                    result->moderatedChannels +=
                        resolved.moderatedChannels.size();

                    auto &items = store().items;
                    auto it = std::find_if(items.begin(), items.end(),
                                           [&](const LimerinoAuthAccount &a) {
                                               return a.userId == old.userId ||
                                                      a.token == old.token;
                                           });
                    if (it != items.end())
                    {
                        mergeResolved(*it, resolved);
                    }
                }
                else
                {
                    ++result->invalid;
                    result->errors.append(
                        QStringLiteral("%1: %2")
                            .arg(old.login.isEmpty()
                                     ? QStringLiteral("<unknown>")
                                     : old.login,
                                 resolved.lastError));

                    auto &items = store().items;
                    auto it = std::find_if(items.begin(), items.end(),
                                           [&](const LimerinoAuthAccount &a) {
                                               return a.userId == old.userId ||
                                                      a.token == old.token;
                                           });
                    if (it != items.end())
                    {
                        it->valid = false;
                        it->lastError = resolved.lastError;
                    }
                }

                if (--(*pending) == 0)
                {
                    sortItems(store().items);
                    save();
                    if (done)
                    {
                        done(*result);
                    }
                }
            });
        };

        // Proactive refresh: before the access token dies, rotate it with the
        // device grant's refresh token. If Twitch refuses to refresh this
        // public client (no secret), validation marks the account invalid
        // with a friendly "sign in again" instead of silently degrading.
        const bool needsRefresh =
            !old.refreshToken.isEmpty() &&
            (!old.expiresAt.isValid() ||
             old.expiresAt < QDateTime::currentDateTime().addSecs(300));
        if (!needsRefresh)
        {
            resolveWith(old.token);
            continue;
        }

        QUrlQuery refreshQuery;
        refreshQuery.addQueryItem(QStringLiteral("refresh_token"),
                                  old.refreshToken);
        refreshQuery.addQueryItem(QStringLiteral("grant_type"),
                                  QStringLiteral("refresh_token"));
        refreshQuery.addQueryItem(QStringLiteral("client_id"), CLIENT_ID);

        NetworkRequest(QUrl(AUTH_TOKEN_URL), NetworkRequestType::Post)
            .header("Content-Type", "application/x-www-form-urlencoded")
            .payload(refreshQuery.query(QUrl::FullyEncoded).toUtf8())
            .hideRequestBody()
            .timeout(REQUEST_TIMEOUT_MS)
            .onSuccess(
                [old, resolveWith](NetworkResult result) {
                    const QJsonObject o = result.parseJson();
                    const QString newToken =
                        o[QStringLiteral("access_token")].toString();
                    if (newToken.isEmpty())
                    {
                        resolveWith(old.token);
                        return;
                    }
                    const QString newRefresh =
                        o[QStringLiteral("refresh_token")].toString();
                    const long expiresIn =
                        o[QStringLiteral("expires_in")].toInt(0);

                    auto &items = store().items;
                    auto it = std::find_if(
                        items.begin(), items.end(),
                        [&](const LimerinoAuthAccount &a) {
                            return a.userId == old.userId ||
                                   a.token == old.token;
                        });
                    if (it != items.end())
                    {
                        it->token = newToken;
                        it->refreshToken = newRefresh.isEmpty()
                                               ? old.refreshToken
                                               : newRefresh;
                        if (expiresIn > 0)
                        {
                            it->expiresAt = QDateTime::currentDateTime()
                                                .addSecs(expiresIn);
                        }
                    }
                    resolveWith(newToken);
                })
            .onError(
                [old, resolveWith](NetworkResult /*result*/) {
                    // Refresh refused; fall through to validating the old one.
                    resolveWith(old.token);
                })
            .execute();
    }
}

void scheduleStartupRefresh()
{
    static bool scheduled = false;
    if (scheduled)
    {
        return;
    }
    scheduled = true;
    QTimer::singleShot(30000, [] { refreshAccounts(); });
}

// ---------------------------------------------------------------------------
// Device login (OAuth 2.0 Device Authorization Grant, RFC 8628 shape)
// ---------------------------------------------------------------------------

namespace {

const QString DEVICE_GRANT_TYPE =
    QStringLiteral("urn:ietf:params:oauth:grant-type:device_code");
constexpr int POLL_FLOOR_MS = 3000;
constexpr int SLOW_DOWN_EXTRA_MS = 5000;

QByteArray formBody(std::initializer_list<std::pair<QString, QString>> items)
{
    QUrlQuery q;
    for (const auto &[key, value] : items)
    {
        q.addQueryItem(key, value);
    }
    return q.query(QUrl::FullyEncoded).toUtf8();
}

// Twitch reports device-flow errors as {"status": 400, "message": "..."};
// RFC 8628 implementations use {"error": "..."} - check both.
QString oauthErrorCode(const QJsonObject &body)
{
    QString code = body[QStringLiteral("error")].toString();
    if (code.isEmpty())
    {
        code = body[QStringLiteral("message")].toString();
    }
    return code;
}

}  // namespace

DeviceLogin::DeviceLogin(QObject *parent)
    : QObject(parent)
{
}

void DeviceLogin::setStatus(State state, const QString &message)
{
    this->status_.state = state;
    this->status_.message = message;
    if (state != State::WaitingForUser)
    {
        this->status_.secondsRemaining = 0;
    }
    this->statusChanged(this->status_);
}

void DeviceLogin::start()
{
    ++this->generation_;
    const quint64 generation = this->generation_;

    this->setStatus(State::RequestingCode,
                    QStringLiteral("Requesting a device code..."));

    NetworkRequest(QUrl(AUTH_DEVICE_URL), NetworkRequestType::Post)
        .header("Content-Type", "application/x-www-form-urlencoded")
        .payload(formBody({{QStringLiteral("client_id"), CLIENT_ID},
                           {QStringLiteral("scopes"), CLIENT_SCOPES}}))
        .timeout(REQUEST_TIMEOUT_MS)
        .onSuccess(
            [guard = QPointer<DeviceLogin>(this),
             generation](NetworkResult result) {
                if (!guard || generation != guard->generation_)
                {
                    return;
                }
                const QJsonObject o = result.parseJson();
                guard->onDeviceCode(
                    o[QStringLiteral("device_code")].toString(),
                    o[QStringLiteral("user_code")].toString(),
                    o[QStringLiteral("verification_uri")].toString(),
                    o[QStringLiteral("interval")].toInt(5),
                    o[QStringLiteral("expires_in")].toInt(1800));
            })
        .onError(
            [guard = QPointer<DeviceLogin>(this),
             generation](NetworkResult result) {
                if (!guard || generation != guard->generation_)
                {
                    return;
                }
                guard->setStatus(
                    State::Failed,
                    QStringLiteral("Could not request a device code: ") +
                        result.formatError());
            })
        .execute();
}

void DeviceLogin::onDeviceCode(const QString &deviceCode,
                               const QString &userCode,
                               const QString &verificationUri, int intervalSec,
                               int expiresInSec)
{
    if (deviceCode.isEmpty() || userCode.isEmpty())
    {
        this->setStatus(State::Failed,
                        QStringLiteral("Unexpected response: no device code or "
                                       "user code in reply."));
        return;
    }

    this->deviceCode_ = deviceCode;
    this->intervalMs_ = std::max(intervalSec * 1000, POLL_FLOOR_MS);
    this->expiresAtMs_ =
        QDateTime::currentMSecsSinceEpoch() + qint64(expiresInSec) * 1000;

    this->status_.userCode = userCode;
    this->status_.verificationUri = verificationUri.isEmpty()
                                        ? QStringLiteral("https://www.twitch.tv/"
                                                         "activate")
                                        : verificationUri;
    this->status_.secondsRemaining = expiresInSec;
    this->setStatus(State::WaitingForUser,
                    QStringLiteral("Open %1 and enter code %2.")
                        .arg(this->status_.verificationUri,
                             this->status_.userCode));

    this->schedulePoll(this->intervalMs_);
}

void DeviceLogin::schedulePoll(int milliseconds)
{
    const quint64 generation = this->generation_;
    QTimer::singleShot(milliseconds, this,
                       [guard = QPointer<DeviceLogin>(this), generation]() {
                           if (!guard || generation != guard->generation_)
                           {
                               return;
                           }
                           guard->poll(generation);
                       });
}

void DeviceLogin::poll(quint64 generation)
{
    if (QDateTime::currentMSecsSinceEpoch() >= this->expiresAtMs_)
    {
        this->status_.userCode.clear();
        this->setStatus(State::Expired,
                        QStringLiteral("The device code expired. Start the "
                                       "login again to get a new one."));
        return;
    }

    this->status_.secondsRemaining =
        int((this->expiresAtMs_ - QDateTime::currentMSecsSinceEpoch()) / 1000);
    this->statusChanged(this->status_);

    NetworkRequest(QUrl(AUTH_TOKEN_URL), NetworkRequestType::Post)
        .header("Content-Type", "application/x-www-form-urlencoded")
        .payload(formBody({{QStringLiteral("client_id"), CLIENT_ID},
                           {QStringLiteral("device_code"), this->deviceCode_},
                           {QStringLiteral("grant_type"), DEVICE_GRANT_TYPE}}))
        .timeout(REQUEST_TIMEOUT_MS)
        .onSuccess(
            [guard = QPointer<DeviceLogin>(this),
             generation](NetworkResult result) {
                if (!guard || generation != guard->generation_)
                {
                    return;
                }
                const QJsonObject o = result.parseJson();
                const QString accessToken =
                    o[QStringLiteral("access_token")].toString();
                if (accessToken.isEmpty())
                {
                    guard->setStatus(State::Failed,
                                     QStringLiteral("Unexpected response: no "
                                                    "access token in reply."));
                    return;
                }
                addOrUpdateToken(LimerinoAuthToken{
                    accessToken,
                    {},
                    {},
                    o[QStringLiteral("refresh_token")].toString(),
                    o[QStringLiteral("expires_in")].toInt(0)});
                guard->deviceCode_.clear();
                guard->status_.userCode.clear();
                guard->setStatus(State::Authorized,
                                 QStringLiteral("Authorized - account added."));
            })
        .onError(
            [guard = QPointer<DeviceLogin>(this),
             generation](NetworkResult result) {
                if (!guard || generation != guard->generation_)
                {
                    return;
                }
                const QString code = oauthErrorCode(result.parseJson());
                if (code == QLatin1String("authorization_pending"))
                {
                    guard->schedulePoll(guard->intervalMs_);
                }
                else if (code == QLatin1String("slow_down"))
                {
                    guard->intervalMs_ += SLOW_DOWN_EXTRA_MS;
                    guard->schedulePoll(guard->intervalMs_);
                }
                else if (code == QLatin1String("access_denied"))
                {
                    guard->deviceCode_.clear();
                    guard->status_.userCode.clear();
                    guard->setStatus(State::Denied,
                                     QStringLiteral("Authorization was "
                                                    "declined."));
                }
                else if (code == QLatin1String("expired_token"))
                {
                    guard->deviceCode_.clear();
                    guard->status_.userCode.clear();
                    guard->setStatus(State::Expired,
                                     QStringLiteral("The device code expired. "
                                                    "Start again."));
                }
                else
                {
                    guard->setStatus(State::Failed,
                                     QStringLiteral("Token request failed: ") +
                                         result.formatError());
                }
            })
        .execute();
}

void DeviceLogin::cancel()
{
    ++this->generation_;  // orphan every in-flight callback for this attempt
    this->deviceCode_.clear();
    this->status_.userCode.clear();
    this->setStatus(State::Idle, QString());
}

// ---------------------------------------------------------------------------
// Feature resolver API (local-only; cached account state)
// ---------------------------------------------------------------------------

namespace {

QString primaryUserId()
{
    const auto user = getApp()->getAccounts()->twitch.getCurrent();
    if (!user || user->isAnon())
    {
        return {};
    }
    return user->getUserId();
}

bool accountCoversChannel(const LimerinoAuthAccount &account,
                          const QString &channelId, const QString &channelLogin)
{
    for (const LimerinoAuthChannel &c : account.moderatedChannels)
    {
        if (!channelId.isEmpty() && !c.id.isEmpty() && c.id == channelId)
        {
            return true;
        }
        if (!channelLogin.isEmpty() && !c.login.isEmpty() &&
            c.login.compare(channelLogin, Qt::CaseInsensitive) == 0)
        {
            return true;
        }
    }
    return false;
}

LimerinoAuthToken makeToken(const LimerinoAuthAccount &a)
{
    return LimerinoAuthToken{a.token, a.userId, a.login};
}

void setErr(QString *err, const QString &message)
{
    if (err != nullptr)
    {
        *err = message;
    }
}

}  // namespace

LimerinoAuthToken resolveModerationToken(const QString &channelId,
                                         const QString &channelLogin,
                                         QString *err)
{
    ensureLoaded();
    const QString pid = primaryUserId();
    const LimerinoAuthAccount *fallback = nullptr;
    for (const LimerinoAuthAccount &a : store().items)
    {
        if (!a.valid || !accountCoversChannel(a, channelId, channelLogin))
        {
            continue;
        }
        if (a.userId == pid)
        {
            return makeToken(a);
        }
        if (fallback == nullptr)
        {
            fallback = &a;
        }
    }
    if (fallback != nullptr)
    {
        return makeToken(*fallback);
    }
    setErr(err, authRequiredMessage(QStringLiteral("moderate in %1")
                                        .arg(channelLogin.isEmpty()
                                                 ? QStringLiteral("this channel")
                                                 : "#" + channelLogin)));
    return {};
}

LimerinoAuthToken resolveBroadcasterToken(const QString &channelId,
                                          const QString &channelLogin,
                                          QString *err)
{
    ensureLoaded();
    const QString pid = primaryUserId();
    const LimerinoAuthAccount *fallback = nullptr;
    for (const LimerinoAuthAccount &a : store().items)
    {
        // Broadcaster-only actions need the broadcaster's own token.
        if (!a.valid || channelId.isEmpty() || a.userId != channelId)
        {
            continue;
        }
        if (a.userId == pid)
        {
            return makeToken(a);
        }
        if (fallback == nullptr)
        {
            fallback = &a;
        }
    }
    if (fallback != nullptr)
    {
        return makeToken(*fallback);
    }
    setErr(err, authRequiredMessage(QStringLiteral("manage %1 as its "
                                                   "broadcaster")
                                        .arg(channelLogin.isEmpty()
                                                 ? QStringLiteral("this channel")
                                                 : "#" + channelLogin)));
    return {};
}

LimerinoAuthToken resolveCurrentUserToken(QString *err)
{
    ensureLoaded();
    const QString pid = primaryUserId();
    if (pid.isEmpty())
    {
        setErr(err, authRequiredMessage(
                        QStringLiteral("act as your signed-in Twitch user")));
        return {};
    }
    const LimerinoAuthAccount *matched = nullptr;
    for (const LimerinoAuthAccount &a : store().items)
    {
        if (a.userId == pid)
        {
            matched = &a;
            break;
        }
    }
    if (matched == nullptr)
    {
        setErr(err, authRequiredMessage(
                        QStringLiteral("act as your signed-in Twitch user")));
        return {};
    }
    if (!matched->valid)
    {
        setErr(err, authExpiredMessage(QStringLiteral("act as %1")
                                           .arg(matched->displayName.isEmpty()
                                                    ? matched->login
                                                    : matched->displayName)));
        return {};
    }
    return makeToken(*matched);
}

LimerinoAuthToken resolveReadToken(QString *err)
{
    ensureLoaded();
    const QString pid = primaryUserId();
    const LimerinoAuthAccount *matched = nullptr;
    for (const LimerinoAuthAccount &a : store().items)
    {
        if (!a.valid)
        {
            continue;
        }
        if (a.userId == pid && !pid.isEmpty())
        {
            return makeToken(a);
        }
        if (matched == nullptr)
        {
            matched = &a;
        }
    }
    if (matched != nullptr)
    {
        return makeToken(*matched);
    }
    setErr(err, authRequiredMessage(QStringLiteral("read chat data")));
    return {};
}

QString authRequiredMessage(const QString &action)
{
    return QStringLiteral(
               "This needs an extra-features sign-in (Settings > Limerino > "
               "Extra features) to %1.")
        .arg(action);
}

QString authExpiredMessage(const QString &action)
{
    return QStringLiteral(
               "The matching extra-features login expired or was revoked. "
               "Re-add it (Settings > Limerino > Extra features) to %1.")
        .arg(action);
}

}  // namespace chatterino::LimerinoAuth
