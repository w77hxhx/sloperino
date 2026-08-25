// SPDX-License-Identifier: MIT
// A single Hermes websocket connection.
//
// Connections are token-grouped: a connection authenticates at most once with
// the token of its first subscription's account (Hermes authenticates per
// connection, not per topic). The current token is fetched at authenticate
// time - reconnects always re-resolve, nothing is captured from an older
// connection.

#pragma once

#include "common/websockets/WebSocketPool.hpp"

#include <QDateTime>
#include <QDebug>
#include <QString>
#include <QTimer>

#include <chrono>
#include <memory>
#include <optional>
#include <unordered_map>
#include <unordered_set>
#include <vector>

namespace chatterino {

template <typename Derived, typename ClientT>
class BasicPubSubManager;

namespace limerino {

class HermesManager;

/// A Hermes topic plus its routing group (the extra-features account whose
/// token authenticates the connection; empty rides any/needs no token).
/// Equality and hashing cover the topic only - a subscription IS its topic,
/// so teardown never depends on credential fields (defect-2-safe by design).
struct HermesSubscription {
    QString topic;
    QString tokenKey;  // account user id (routing group), "" = unauthenticated

    bool operator==(const HermesSubscription &other) const
    {
        return this->topic == other.topic;
    }

    friend QDebug operator<<(QDebug debug, const HermesSubscription &data);
};

}  // namespace limerino

}  // namespace chatterino

template <>
struct std::hash<chatterino::limerino::HermesSubscription> {
    std::size_t operator()(
        const chatterino::limerino::HermesSubscription &data) const noexcept
    {
        return std::hash<QString>{}(data.topic);
    }
};

namespace chatterino::limerino {

class HermesClient : public std::enable_shared_from_this<HermesClient>
{
public:
    using Subscription = HermesSubscription;

    // client.js L30: MAX_TOPICS_PER_CONNECTION = 100
    static constexpr size_t MAX_SUBSCRIPTIONS = 100;

    explicit HermesClient(HermesManager &manager);
    ~HermesClient();
    HermesClient(const HermesClient &) = delete;
    HermesClient(HermesClient &&) = delete;
    HermesClient &operator=(const HermesClient &) = delete;
    HermesClient &operator=(HermesClient &&) = delete;

    // --- BasicPubSubManager contract (statically dispatched) ---
    const size_t maxSubscriptions = MAX_SUBSCRIPTIONS;

    /// GUI thread (marshalled by the listener).
    void onOpen();
    /// Websocket thread; marshals to GUI before touching any state.
    void onMessage(const QByteArray &msg);
    void close();
    bool isOpen() const;

    /// @return false if this client declined the subscription (full or a
    ///         different token group); true if accepted (queued or wired).
    bool subscribe(const Subscription &sub);
    /// @return true if this client had the topic and dropped it.
    bool unsubscribe(const Subscription &sub);
    bool isSubscribed(const Subscription &sub) const;

    /// Re-enter the authentication flow after it stalled (token unavailable
    /// or auth failures cleared by the controller).
    void retryAuthentication();

private:
    enum class AuthState {
        Idle,          // connected, nothing authenticated yet
        Authenticating,
        Authenticated,
        Unauthenticated,  // no token needed/available; subscribes go out raw
        Unavailable,      // a token is required but none is resolvable now
        Failed,
    };

    bool canAccept(const QString &tokenKey) const;
    void driveWire();
    void beginAuthentication();
    void flushQueued();
    void sendSubscribe(const Subscription &sub);

    /// GUI thread.
    void handleMessage(const QByteArray &msg);
    void onWelcome(const QJsonObject &object);
    void onAuthenticateResponse(const QJsonObject &object);
    void onSubscribeResponse(const QJsonObject &object);
    void onUnsubscribeResponse(const QJsonObject &object);
    void onNotification(const QJsonObject &object);
    void checkKeepalive();

    HermesManager &manager_;

public:
    // --- manager-protocol members ---
    // Read/written directly by BasicPubSubManager (salvage, socket wiring)
    // and HermesManager (grouping/sweep queries). Public by design: this
    // class exists only to serve those two.
    /// Accepted subscriptions (queued or wire-sent). Salvaged as a whole by
    /// BasicPubSubManager::onConnectionClose.
    std::unordered_set<Subscription> subscriptions_;
    WebSocketHandle ws_;
    std::optional<QString> identityKey_;  // unset until first subscription

private:
    /// Accepted but not yet on the wire (waiting for authentication).
    std::vector<Subscription> queued_;

    /// Wire id <-> topic (ids exist only for wire-sent subscriptions).
    std::unordered_map<QString, QString> idToTopic_;
    std::unordered_map<QString, QString> topicToId_;

    AuthState authState_ = AuthState::Idle;
    bool open_ = false;

    QTimer keepaliveTimer_;
    std::chrono::milliseconds keepaliveMs_{12500};
    QDateTime lastKeepaliveAt_;
};

}  // namespace chatterino::limerino
