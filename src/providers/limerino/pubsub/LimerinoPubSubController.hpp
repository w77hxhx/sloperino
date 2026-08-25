// SPDX-License-Identifier: MIT
// Limerino live-updates controller: owns the *intent* ("this topic should be
// subscribed, as this account") and derives all transport truth from
// observable sink events - no parallel belief map (defect 3).
//
//   - failed subscribe -> exponential backoff, capped attempts, terminal
//     failure surfaced in the Limerino settings page (defect 1);
//   - subscribe/unsubscribe by topic string only (defect 2);
//   - tokens are resolved at submission *and* at connection-authenticate time
//     (resolveLiveToken), so reconnects never reuse a captured token;
//   - account add/remove/switch -> reconcile(): re-resolve, re-key, sweep
//     foreign-user topics (forgetOtherUserAuthenticatedTopics analogue).
//
// All state changes run on the GUI thread.

#pragma once

#include "util/ExponentialBackoff.hpp"

#include <pajlada/signals/signal.hpp>
#include <pajlada/signals/signalholder.hpp>
#include <QJsonObject>
#include <QSet>
#include <QString>
#include <QStringList>

#include <chrono>
#include <cstdint>
#include <functional>
#include <memory>
#include <optional>
#include <unordered_map>
#include <vector>

namespace chatterino::limerino {

/// Which extra-features account a topic authenticates as.
enum class PubSubTopicAuth {
    None,  // no token required (may still ride an authenticated connection)
    User,  // resolveCurrentUserToken: account matching the primary login
};

/// One notification for the events channel (+ future feature consumers).
/// eventType is the inner payload's "type", verbatim from the wire.
/// category groups topics for coloured chips in the events view ("moderation",
/// "points", "prediction", "poll", "raid", "follow", "event"); payload is the
/// raw notification, kept for tooltips/debug only - never rendered as text.
struct PubSubEvent {
    QString topic;
    QString channelId;    // topic suffix annotation (client.js L182, L461)
    QString eventType;
    QString category;     // controller-derived from the topic prefix
    QJsonObject payload;  // raw notification (tooltip / copy-raw)
    QString displayText;  // single-line, user-facing summary
    // When set, displayText may still contain this numeric id; the controller
    // resolves it to a login before eventProduced (E1.b).
    QString displayChannelId;
};

/// Transport truth as seen through the sink.
struct PubSubTransportStatus {
    size_t connections = 0;
    uint64_t connectionsOpened = 0;
    uint64_t connectionsClosed = 0;
    uint64_t connectionsFailed = 0;
    uint64_t notificationsReceived = 0;
    uint64_t subscribeResponses = 0;
    uint64_t failedSubscribeResponses = 0;
    uint64_t authFailures = 0;
    uint64_t reconnectsReceived = 0;
    uint64_t keepalivesMissed = 0;
};

/// Sink seam: every side effect of the controller. Production implementation
/// wraps HermesManager; tests substitute a recording double.
class IPubSubSink
{
public:
    virtual ~IPubSubSink() = default;

    virtual void listen(const QString &topic, const QString &tokenKey) = 0;
    virtual void unlisten(const QString &topic) = 0;
    virtual void relisten(const QString &topic, const QString &tokenKey) = 0;

    /// topic -> tokenKey for everything actually accepted on connections
    /// right now (sweep input).
    virtual std::unordered_map<QString, QString> subscribedTopics() const = 0;
    virtual PubSubTransportStatus transportStatus() const = 0;

    /// Re-enter authentication on connections stalled for this key.
    virtual void retryAuthentication(const QString &tokenKey) = 0;

    virtual pajlada::Signals::Signal<const QString &>
        &subscribeSucceeded() = 0;
    virtual pajlada::Signals::Signal<const QString &, const QString &>
        &subscribeFailed() = 0;
    virtual pajlada::Signals::Signal<const QString &, const QString &>
        &authSucceeded() = 0;
    virtual pajlada::Signals::Signal<const QString &, const QString &>
        &authFailed() = 0;
    virtual pajlada::Signals::Signal<const QString &, const QString &>
        &authUnavailable() = 0;
    virtual pajlada::Signals::Signal<const QString &, const QJsonObject &>
        &topicMessage() = 0;
};

/// Resolve a topic's account right now. token+userId empty = unresolvable;
/// reason is user-facing and contains no credential material.
struct PubSubTokenResolution {
    QString token;
    QString userId;
    QString reason;
};
using PubSubTokenResolver =
    std::function<PubSubTokenResolution(PubSubTopicAuth kind)>;

class LimerinoPubSubController
{
public:
    struct Config {
        std::chrono::milliseconds retryBase{2000};
        int maxAttempts = 5;      // failed subscribes before terminal failure
        int maxAuthFailures = 2;  // consecutive auth failures before failing its topics
    };

    enum class TopicState {
        Pending,   // submitted, awaiting subscribeResponse
        Active,
        Retrying,  // waiting out backoff before an automatic re-submit
        Failed,    // exhausted retries (or repeated auth failure) - surfaced
        AuthBlocked,  // extra auth missing/expired: NOT subscribed (rule 5)
    };

    struct TopicStatus {
        QString topic;
        PubSubTopicAuth auth = PubSubTopicAuth::None;
        QString accountUserId;
        TopicState state = TopicState::Pending;
        int attempts = 0;
        QString lastError;
    };

    struct DiagSnapshot {
        PubSubTransportStatus transport;
        int topicsActive = 0;
        int topicsPending = 0;
        int topicsRetrying = 0;
        int topicsFailed = 0;
        int topicsBlocked = 0;
        QString lastErrorTopic;
        QString lastError;
    };

    LimerinoPubSubController(std::unique_ptr<IPubSubSink> sink,
                             PubSubTokenResolver resolver, Config config);
    ~LimerinoPubSubController();
    LimerinoPubSubController(const LimerinoPubSubController &) = delete;
    LimerinoPubSubController &operator=(const LimerinoPubSubController &) =
        delete;

    /// Idempotent: ensure this topic exists, authenticated as `auth` requires.
    void ensureTopic(const QString &topic, PubSubTopicAuth auth);
    void dropTopic(const QString &topic);

    /// Clears terminal failures (topics + auth gates) and resubmits.
    void retryFailed();

    // --- account lifecycle ---
    /// Re-resolve every topic's account; re-key changed topics; drop topics
    /// of vanished accounts; unlisten topics that belong to no current
    /// account (cross-account leak sweep).
    void reconcile();

    /// authenticate-time token lookup for connections (fresh every call).
    std::optional<QString> resolveLiveToken(const QString &tokenKey,
                                            QString *reason);

    std::vector<TopicStatus> topicStatuses() const;
    DiagSnapshot diagSnapshot() const;

    /// Per-topic parsing/formatting extension point. Handlers match by exact
    /// topic prefix; first match wins (longest prefix first). Handlers fill
    /// event.displayText (and may drive their own feature side effects) and
    /// return whether the message was understood. The generic PubSubEvent is
    /// emitted for every notification regardless (events channel sees all).
    using TopicMessageHandler =
        std::function<bool(const QString &topic, const QJsonObject &payload,
                           PubSubEvent &event)>;
    void registerTopicHandler(const QString &prefix,
                              TopicMessageHandler handler);

    /// Event types the filter dialog can list: explicitly registered plus
    /// everything seen on the wire so far.
    void registerKnownEventType(const QString &type);
    QStringList knownEventTypes() const;

    pajlada::Signals::Signal<const PubSubEvent &> eventProduced;
    pajlada::Signals::NoArgSignal diagChanged;

private:
    struct TopicEntry {
        TopicStatus status;
        int retryGeneration = 0;
        ExponentialBackoff<8> backoff;
        explicit TopicEntry(std::chrono::milliseconds base)
            : backoff(base)
        {
        }
    };

    void onSubscribeSucceeded(const QString &topic);
    void onSubscribeFailed(const QString &topic, const QString &error);
    void onAuthSucceeded(const QString &tokenKey);
    void onAuthFailed(const QString &tokenKey, const QString &error);
    void onAuthUnavailable(const QString &tokenKey, const QString &reason);
    void onTopicMessage(const QString &topic, const QJsonObject &payload);

    /// @return nullptr when the topic isn't registered anymore.
    TopicEntry *entryFor(const QString &topic);
    void submitTopic(TopicEntry &entry);
    void clearAuthGates();

    std::unique_ptr<IPubSubSink> sink_;
    PubSubTokenResolver resolver_;
    Config config_;

    std::unordered_map<QString, TopicEntry> entries_;
    QSet<QString> blockedAuthKeys_;
    std::unordered_map<QString, int> authFailCounts_;

    std::vector<std::pair<QString, TopicMessageHandler>> handlers_;
    QSet<QString> knownEventTypes_;

    /// Guards async callbacks (deferred wiring + backoff timers) against a
    /// destroyed controller (tests construct/destroy instances).
    std::shared_ptr<bool> aliveGuard_ = std::make_shared<bool>(true);

    pajlada::Signals::SignalHolder holder_;
};

/// Singleton access (GUI thread). Created on first call.
LimerinoPubSubController *getPubSubController();

/// Idempotent bootstrap; called once from the application's startup path.
void initializePubSub();

}  // namespace chatterino::limerino
