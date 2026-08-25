// SPDX-License-Identifier: MIT
// Hermes connection pool + outward signals. Owns no topic policy: the
// controller (LimerinoPubSubController) decides which topics exist and with
// which account; this manager transports and reports.

#pragma once

#include "providers/liveupdates/BasicPubSubManager.hpp"
#include "providers/limerino/pubsub/HermesClient.hpp"

#include <pajlada/signals/signal.hpp>
#include <QJsonObject>
#include <QString>

#include <cstdint>
#include <functional>
#include <memory>
#include <optional>
#include <unordered_map>

namespace chatterino::limerino {

/// Transport-level counters (names follow the reference's metrics,
/// client.js L10-16).
struct HermesDiag {
    std::atomic<uint64_t> messagesReceived{0};
    std::atomic<uint64_t> messagesFailedToParse{0};
    std::atomic<uint64_t> notificationsReceived{0};
    std::atomic<uint64_t> subscribeResponses{0};
    std::atomic<uint64_t> failedSubscribeResponses{0};
    std::atomic<uint64_t> authFailures{0};
    std::atomic<uint64_t> reconnectsReceived{0};
    std::atomic<uint64_t> keepalivesMissed{0};
};

class HermesManager : public BasicPubSubManager<HermesManager, HermesClient>
{
public:
    HermesManager();
    ~HermesManager() override;
    HermesManager(const HermesManager &) = delete;
    HermesManager(HermesManager &&) = delete;
    HermesManager &operator=(const HermesManager &) = delete;
    HermesManager &operator=(HermesManager &&) = delete;

    std::shared_ptr<HermesClient> makeClient();  // manager concept

    void listen(const QString &topic, const QString &tokenKey);
    void unlisten(const QString &topic);
    /// Defect-1 recovery vehicle: drop + resubmit in one call.
    void relisten(const QString &topic, const QString &tokenKey);
    /// Closes every connection; the pool salvages and re-drives the topics.
    void reconnect();

    /// Resolves the *current* token for a connection's identity key at
    /// authenticate time. Empty key = any workable token (unauth topics may
    /// ride an authenticated connection, client.js subscribes everything on
    /// one authenticated connection).
    using AuthResolver = std::function<std::optional<QString>(
        const QString &tokenKey, QString *reason)>;
    void setAuthResolver(AuthResolver resolver);
    std::optional<QString> resolveToken(const QString &tokenKey,
                                        QString *reason);

    /// topic -> tokenKey for every accepted subscription on any connection
    /// (used by the account-switch sweep; no parallel belief map).
    std::unordered_map<QString, QString> subscribedTopics() const;
    size_t connectionCount() const;

    /// Forwards retryAuthentication() to connections stalled on this key.
    void retryAuthentication(const QString &tokenKey);

    HermesDiag hermesDiag;
    const liveupdates::Diag &wsDiag() const;

    // --- client -> manager callbacks (GUI thread) ---
    void clientAuthSucceeded(HermesClient *client, const QString &tokenKey);
    void clientAuthFailed(HermesClient *client, const QString &tokenKey,
                          const QString &error);
    void clientAuthUnavailable(HermesClient *client, const QString &tokenKey,
                               const QString &reason);
    void clientSubscribeSucceeded(HermesClient *client, const QString &topic);
    void clientSubscribeFailed(HermesClient *client, const QString &topic,
                               const QString &error);
    void clientTopicMessage(const QString &topic, const QJsonObject &payload);
    void clientReconnectRequested();
    void clientMissedKeepalive();

    // --- outward signals (GUI thread) ---
    pajlada::Signals::Signal<const QString &> subscribeSucceeded;
    pajlada::Signals::Signal<const QString &, const QString &> subscribeFailed;
    pajlada::Signals::Signal<const QString &, const QString &> authSucceeded;
    pajlada::Signals::Signal<const QString &, const QString &> authFailed;
    pajlada::Signals::Signal<const QString &, const QString &> authUnavailable;
    pajlada::Signals::Signal<const QString &, const QJsonObject &> topicMessage;

private:
    AuthResolver authResolver_;

    friend BasicPubSubManager<HermesManager, HermesClient>;
};

}  // namespace chatterino::limerino
