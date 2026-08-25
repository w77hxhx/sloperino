// SPDX-License-Identifier: MIT
// Secondary "Limerino Extra Features" auth.
// Completely separate store from the primary Twitch login (/accounts/uid<id>/).
// See FORK.md.

#pragma once

#include <QDateTime>
#include <QObject>
#include <QString>
#include <QStringList>
#include <QVector>

#include <pajlada/signals/signal.hpp>

#include <functional>

namespace chatterino::LimerinoAuth {

struct LimerinoAuthChannel {
    QString id;
    QString login;
    QString displayName;

    bool operator==(const LimerinoAuthChannel &other) const
    {
        return this->id == other.id && this->login == other.login &&
               this->displayName == other.displayName;
    }
};

struct LimerinoAuthAccount {
    QString userId;
    QString login;
    QString displayName;
    QString token;
    bool valid = false;
    QString lastError;
    QDateTime lastValidatedAt;
    // Granted scopes as reported by /oauth2/validate (empty before first validation).
    QStringList scopes;
    // Device-grant refresh token (may be empty for pasted/script tokens).
    QString refreshToken;
    // Access-token expiry as reported by the token endpoint/validate (may be invalid).
    QDateTime expiresAt;
    QVector<LimerinoAuthChannel> moderatedChannels;
};

struct LimerinoAuthToken {
    QString token;
    QString userId;
    QString login;
    // Optional: device-grant refresh token + lifetime (seconds), persisted on the account.
    QString refreshToken;
    long expiresInSec = 0;

    bool hasToken() const
    {
        return !this->token.isEmpty();
    }
};

struct LimerinoAuthSummary {
    int accountCount = 0;
    int validAccountCount = 0;
    int invalidAccountCount = 0;
    int moderatedChannelCount = 0;
};

struct LimerinoAuthRefreshResult {
    int total = 0;
    int valid = 0;
    int invalid = 0;
    int moderatedChannels = 0;
    QStringList errors;
};

// Trim, strip wrapping quotes and common header prefixes, loop until stable.
// Users paste raw header values; this makes pasting "Authorization: Bearer xyz" safe.
QString normalizeToken(QString raw);

QVector<LimerinoAuthAccount> accounts();
LimerinoAuthSummary summary();

// Token must already be normalized. Validation happens asynchronously;
// the account appears (possibly unvalidated) immediately via accountsChanged.
void addOrUpdateToken(const LimerinoAuthToken &token);
void removeAccount(const QString &userId);

// Re-validates every stored account. `done` runs once, on the GUI thread.
void refreshAccounts(
    const std::function<void(const LimerinoAuthRefreshResult &)> &done = {});

// One-shot deferred refresh, safe to call from app startup. Runs at most once.
void scheduleStartupRefresh();

// Emitted whenever the account store changes (add/remove/update/validate).
extern pajlada::Signals::NoArgSignal accountsChanged;

// Device Authorization Grant flow state machine (OAuth 2.0 RFC 8628 shape).
// Owns an entire login attempt; the UI connects to statusChanged and may call
// cancel() at any point. A generation counter plus QPointer guards ensure a
// cancelled/restarted attempt can never be mutated by stale callbacks.
class DeviceLogin final : public QObject
{
    Q_OBJECT

public:
    explicit DeviceLogin(QObject *parent = nullptr);

    enum class State {
        Idle,
        RequestingCode,
        WaitingForUser,
        Authorized,
        Denied,
        Expired,
        Failed,
    };

    struct Status {
        State state = State::Idle;
        // User-facing text; NEVER contains token material or the device code.
        QString message;
        QString userCode;
        QString verificationUri;
        int secondsRemaining = 0;
    };

    void start();
    void cancel();

    Status status() const
    {
        return this->status_;
    }

Q_SIGNALS:
    void statusChanged(const LimerinoAuth::DeviceLogin::Status &status);

private:
    void setStatus(State state, const QString &message);
    void schedulePoll(int milliseconds);
    void poll(quint64 generation);
    void onDeviceCode(const QString &deviceCode, const QString &userCode,
                      const QString &verificationUri, int intervalSec,
                      int expiresInSec);

    Status status_;
    quint64 generation_ = 0;
    QString deviceCode_;  // kept out of Status/messages deliberately
    int intervalMs_ = 3000;
    qint64 expiresAtMs_ = 0;
};

// --- exposed for the device login flow (Phase 2) and feature resolver (Phase 5) ---

// Asynchronously validates `normalizedToken` against Twitch and produces a fully
// resolved account (valid flag, identity, display name, moderated channels).
void resolveToken(const QString &normalizedToken,
                  const std::function<void(const LimerinoAuthAccount &)> &onDone);

// Device flow constants (Phase 2). Client id is the Twitch web / front-end
// first-party client (see LimerinoAuth.cpp for provenance); the wider scope
// list is requested so Helix fallback paths are authorized.
extern const QString CLIENT_ID;
extern const QString CLIENT_SCOPES;
extern const QString AUTH_DEVICE_URL;
extern const QString AUTH_TOKEN_URL;

// --- feature resolver API (Phase 5) ---
// Local-only resolution over the cached account store; never a network call.
// `err` (optional) receives a friendly, token-free failure reason.
LimerinoAuthToken resolveModerationToken(const QString &channelId,
                                         const QString &channelLogin,
                                         QString *err = nullptr);
LimerinoAuthToken resolveBroadcasterToken(const QString &channelId,
                                          const QString &channelLogin,
                                          QString *err = nullptr);
LimerinoAuthToken resolveCurrentUserToken(QString *err = nullptr);
LimerinoAuthToken resolveReadToken(QString *err = nullptr);

// Consistent, friendly error texts for feature code.
QString authRequiredMessage(const QString &action);
QString authExpiredMessage(const QString &action);

}  // namespace chatterino::LimerinoAuth
