// SPDX-License-Identifier: MIT

#include "providers/limerino/pubsub/LimerinoPubSubController.hpp"

#include "Application.hpp"
#include "controllers/accounts/AccountController.hpp"
#include "debug/AssertInGuiThread.hpp"
#include "providers/limerino/LimerinoAuth.hpp"
#include "providers/limerino/pubsub/HermesChannelTopics.hpp"
#include "providers/limerino/pubsub/HermesManager.hpp"
#include "providers/limerino/pubsub/HermesUserTopics.hpp"
#include "providers/limerino/pubsub/LimerinoChannelNameResolver.hpp"
#include "providers/limerino/pubsub/LimerinoPubSubEventDedupe.hpp"
#include "providers/limerino/pubsub/LimerinoPubSubTopics.hpp"
#include "singletons/Settings.hpp"

#include <QTimer>

#include <algorithm>

namespace chatterino::limerino {

namespace {

/// IPubSubSink over the real Hermes transport.
class HermesManagerSink : public IPubSubSink
{
public:
    explicit HermesManagerSink(std::unique_ptr<HermesManager> manager)
        : manager_(std::move(manager))
    {
    }

    HermesManager &manager()
    {
        return *this->manager_;
    }

    void listen(const QString &topic, const QString &tokenKey) override
    {
        this->manager_->listen(topic, tokenKey);
    }
    void unlisten(const QString &topic) override
    {
        this->manager_->unlisten(topic);
    }
    void relisten(const QString &topic, const QString &tokenKey) override
    {
        this->manager_->relisten(topic, tokenKey);
    }

    std::unordered_map<QString, QString> subscribedTopics() const override
    {
        return this->manager_->subscribedTopics();
    }

    PubSubTransportStatus transportStatus() const override
    {
        PubSubTransportStatus s;
        s.connections = this->manager_->connectionCount();
        const auto &ws = this->manager_->wsDiag();
        s.connectionsOpened = ws.connectionsOpened.load();
        s.connectionsClosed = ws.connectionsClosed.load();
        s.connectionsFailed = ws.connectionsFailed.load();
        const auto &d = this->manager_->hermesDiag;
        s.notificationsReceived = d.notificationsReceived.load();
        s.subscribeResponses = d.subscribeResponses.load();
        s.failedSubscribeResponses = d.failedSubscribeResponses.load();
        s.authFailures = d.authFailures.load();
        s.reconnectsReceived = d.reconnectsReceived.load();
        s.keepalivesMissed = d.keepalivesMissed.load();
        return s;
    }

    void retryAuthentication(const QString &tokenKey) override
    {
        this->manager_->retryAuthentication(tokenKey);
    }

    pajlada::Signals::Signal<const QString &> &subscribeSucceeded() override
    {
        return this->manager_->subscribeSucceeded;
    }
    pajlada::Signals::Signal<const QString &, const QString &>
        &subscribeFailed() override
    {
        return this->manager_->subscribeFailed;
    }
    pajlada::Signals::Signal<const QString &, const QString &>
        &authSucceeded() override
    {
        return this->manager_->authSucceeded;
    }
    pajlada::Signals::Signal<const QString &, const QString &>
        &authFailed() override
    {
        return this->manager_->authFailed;
    }
    pajlada::Signals::Signal<const QString &, const QString &>
        &authUnavailable() override
    {
        return this->manager_->authUnavailable;
    }
    pajlada::Signals::Signal<const QString &, const QJsonObject &>
        &topicMessage() override
    {
        return this->manager_->topicMessage;
    }

private:
    std::unique_ptr<HermesManager> manager_;
};

/// Production token resolution: exclusively LimerinoAuth, never the primary
/// login's raw token (hard rule 5).
PubSubTokenResolution resolveWithLimerinoAuth(PubSubTopicAuth kind)
{
    if (kind == PubSubTopicAuth::None)
    {
        return {};
    }
    QString err;
    auto token = LimerinoAuth::resolveCurrentUserToken(&err);
    if (!token.hasToken())
    {
        return {{}, {}, err};
    }
    return {token.token, token.userId, {}};
}

LimerinoPubSubController *g_instance = nullptr;

}  // namespace

LimerinoPubSubController::LimerinoPubSubController(
    std::unique_ptr<IPubSubSink> sink, PubSubTokenResolver resolver,
    Config config)
    : sink_(std::move(sink))
    , resolver_(std::move(resolver))
    , config_(config)
{
    this->holder_.managedConnect(this->sink_->subscribeSucceeded(),
                                 [this](const QString &topic) {
                                     this->onSubscribeSucceeded(topic);
                                 });
    this->holder_.managedConnect(
        this->sink_->subscribeFailed(),
        [this](const QString &topic, const QString &error) {
            this->onSubscribeFailed(topic, error);
        });
    this->holder_.managedConnect(this->sink_->authSucceeded(),
                                 [this](const QString &key, const QString &) {
                                     this->onAuthSucceeded(key);
                                 });
    this->holder_.managedConnect(this->sink_->authFailed(),
                                 [this](const QString &key, const QString &err) {
                                     this->onAuthFailed(key, err);
                                 });
    this->holder_.managedConnect(
        this->sink_->authUnavailable(),
        [this](const QString &key, const QString &reason) {
            this->onAuthUnavailable(key, reason);
        });
    this->holder_.managedConnect(
        this->sink_->topicMessage(),
        [this](const QString &topic, const QJsonObject &payload) {
            this->onTopicMessage(topic, payload);
        });

    this->holder_.managedConnect(LimerinoAuth::accountsChanged,
                                 [this] { this->reconcile(); });

    // Deferred: account-switch hook (needs Application to exist).
    std::weak_ptr<bool> alive = this->aliveGuard_;
    QTimer::singleShot(0, [this, alive] {
        if (alive.expired())
        {
            return;
        }
        auto *app = tryGetApp();
        if (app != nullptr)
        {
            this->holder_.managedConnect(
                app->getAccounts()->twitch.currentUserChanged,
                [this] { this->reconcile(); });
        }
    });
}

LimerinoPubSubController::~LimerinoPubSubController() = default;

LimerinoPubSubController::TopicEntry *LimerinoPubSubController::entryFor(
    const QString &topic)
{
    auto it = this->entries_.find(topic);
    if (it == this->entries_.end())
    {
        return nullptr;
    }
    return &it->second;
}

void LimerinoPubSubController::submitTopic(TopicEntry &entry)
{
    entry.status.state = TopicState::Pending;
    this->sink_->listen(entry.status.topic, entry.status.accountUserId);
}

void LimerinoPubSubController::ensureTopic(const QString &topic,
                                           PubSubTopicAuth auth)
{
    auto it = this->entries_.find(topic);
    if (it != this->entries_.end())
    {
        if (it->second.status.auth == auth)
        {
            return;  // already ensured
        }
        this->sink_->unlisten(topic);
        this->entries_.erase(it);
    }

    auto [placed, inserted] = this->entries_.emplace(
        topic, TopicEntry(this->config_.retryBase));
    placed->second.status.topic = topic;
    placed->second.status.auth = auth;
    (void)inserted;

    if (auth == PubSubTopicAuth::None)
    {
        this->submitTopic(placed->second);
        this->diagChanged.invoke();
        return;
    }

    auto resolution = this->resolver_(auth);
    if (resolution.userId.isEmpty())
    {
        // Extra auth absent/expired: the topic is never submitted (rule 5).
        placed->second.status.state = TopicState::AuthBlocked;
        placed->second.status.lastError = resolution.reason;
    }
    else
    {
        placed->second.status.accountUserId = resolution.userId;
        this->submitTopic(placed->second);
    }
    this->diagChanged.invoke();
}

void LimerinoPubSubController::dropTopic(const QString &topic)
{
    auto it = this->entries_.find(topic);
    if (it == this->entries_.end())
    {
        return;
    }
    it->second.retryGeneration++;
    if (it->second.status.state != TopicState::AuthBlocked)
    {
        this->sink_->unlisten(topic);
    }
    this->entries_.erase(it);
    this->diagChanged.invoke();
}

void LimerinoPubSubController::retryFailed()
{
    assertInGuiThread();
    this->clearAuthGates();

    QSet<QString> keysToNudge;
    for (auto &[topic, entry] : this->entries_)
    {
        if (entry.status.state != TopicState::Failed)
        {
            continue;
        }
        entry.retryGeneration++;
        entry.status.attempts = 0;
        entry.status.lastError.clear();
        entry.backoff.reset();
        if (entry.status.auth == PubSubTopicAuth::User)
        {
            auto resolution = this->resolver_(PubSubTopicAuth::User);
            if (resolution.userId.isEmpty())
            {
                entry.status.state = TopicState::AuthBlocked;
                entry.status.lastError = resolution.reason;
                entry.status.accountUserId.clear();
                continue;
            }
            entry.status.accountUserId = resolution.userId;
        }
        this->submitTopic(entry);
        if (entry.status.auth == PubSubTopicAuth::User)
        {
            keysToNudge.insert(entry.status.accountUserId);
        }
    }
    for (const auto &key : keysToNudge)
    {
        this->sink_->retryAuthentication(key);
    }
    this->diagChanged.invoke();
}

void LimerinoPubSubController::reconcile()
{
    assertInGuiThread();
    this->clearAuthGates();

    // Topic renames on account switch: user topics embed the user id in the
    // topic string (chatrooms-user-v1.<uid>); re-issuing as the new account
    // means rebuilding the topic string, not re-keying the old one.
    struct Rename {
        QString oldTopic;
        QString newTopic;
        QString newUserId;
    };
    std::vector<Rename> renames;
    QSet<QString> allowedUsers;

    for (auto &[topic, entry] : this->entries_)
    {
        auto &status = entry.status;
        if (status.auth != PubSubTopicAuth::User)
        {
            continue;
        }

        auto resolution = this->resolver_(PubSubTopicAuth::User);
        if (resolution.userId.isEmpty())
        {
            if (status.state != TopicState::AuthBlocked)
            {
                entry.retryGeneration++;
                this->sink_->unlisten(status.topic);
                status.state = TopicState::AuthBlocked;
            }
            status.lastError = resolution.reason;
            status.accountUserId.clear();
            continue;
        }

        if (status.state == TopicState::AuthBlocked)
        {
            status.accountUserId = resolution.userId;
            status.lastError.clear();
            entry.retryGeneration++;
            entry.status.attempts = 0;
            entry.backoff.reset();
            this->submitTopic(entry);
        }
        else if (status.accountUserId != resolution.userId)
        {
            const QString oldSuffix = userIdFromUserTopic(status.topic);
            if (!oldSuffix.isEmpty() && oldSuffix != resolution.userId)
            {
                renames.push_back(
                    {status.topic,
                     status.topic.left(status.topic.size() - oldSuffix.size()) +
                         resolution.userId,
                     resolution.userId});
            }
            else
            {
                // Topic string doesn't identify a user: cannot be re-issued
                // correctly - drop the submission (rule 5 applies).
                entry.retryGeneration++;
                this->sink_->unlisten(status.topic);
                status.state = TopicState::AuthBlocked;
                status.accountUserId.clear();
                status.lastError = QStringLiteral(
                    "topic cannot be re-issued for the new account");
            }
        }

        allowedUsers.insert(resolution.userId);
    }

    for (auto &rename : renames)
    {
        auto it = this->entries_.find(rename.oldTopic);
        if (it == this->entries_.end() ||
            this->entries_.contains(rename.newTopic))
        {
            continue;
        }
        this->sink_->unlisten(rename.oldTopic);
        auto node = this->entries_.extract(it);
        node.key() = rename.newTopic;
        auto &entry = node.mapped();
        entry.status.topic = rename.newTopic;
        entry.status.accountUserId = rename.newUserId;
        entry.status.attempts = 0;
        entry.status.lastError.clear();
        entry.retryGeneration++;
        entry.backoff.reset();
        auto result = this->entries_.insert(std::move(node));
        this->submitTopic(result.position->second);
    }

    // No User-kind topic may exist for a user that no entry allows
    // (forgetOtherUserAuthenticatedTopics analogue).
    for (const auto &[topic, key] : this->sink_->subscribedTopics())
    {
        const QString uid = userIdFromUserTopic(topic);
        if (uid.isEmpty() || allowedUsers.contains(uid))
        {
            continue;
        }
        this->sink_->unlisten(topic);
    }

    this->diagChanged.invoke();
}

std::optional<QString> LimerinoPubSubController::resolveLiveToken(
    const QString &tokenKey, QString *reason)
{
    auto deny = [&](const QString &why) {
        if (reason != nullptr)
        {
            *reason = why;
        }
        return std::optional<QString>();
    };

    if (this->blockedAuthKeys_.contains(tokenKey))
    {
        return deny(QStringLiteral(
            "authentication for this account failed repeatedly; fix the "
            "extra-features sign-in and press \"Retry failed listens\""));
    }

    if (tokenKey.isEmpty())
    {
        QString err;
        auto token = LimerinoAuth::resolveReadToken(&err);
        if (token.hasToken())
        {
            return token.token;
        }
        return deny(err.isEmpty() ? QStringLiteral(
                                        "no extra-features account available")
                                  : err);
    }

    for (const auto &account : LimerinoAuth::accounts())
    {
        if (account.userId != tokenKey)
        {
            continue;
        }
        if (account.valid)
        {
            return account.token;
        }
        return deny(account.lastError.isEmpty()
                        ? QStringLiteral("the account's sign-in is invalid")
                        : account.lastError);
    }
    return deny(
        QStringLiteral("no extra-features account matches this topic's user"));
}

void LimerinoPubSubController::clearAuthGates()
{
    this->blockedAuthKeys_.clear();
    this->authFailCounts_.clear();
}

// ---- sink event handlers ----

void LimerinoPubSubController::onSubscribeSucceeded(const QString &topic)
{
    auto *entry = this->entryFor(topic);
    if (entry == nullptr)
    {
        return;  // subscribed by an older intent; nothing to do
    }
    entry->status.state = TopicState::Active;
    entry->status.attempts = 0;
    entry->status.lastError.clear();
    entry->backoff.reset();
    this->diagChanged.invoke();
}

void LimerinoPubSubController::onSubscribeFailed(const QString &topic,
                                                 const QString &error)
{
    auto *entry = this->entryFor(topic);
    if (entry == nullptr || entry->status.state == TopicState::Failed)
    {
        return;
    }

    entry->status.attempts++;
    entry->status.lastError = error;

    if (entry->status.attempts >= this->config_.maxAttempts)
    {
        entry->status.state = TopicState::Failed;
    }
    else
    {
        entry->status.state = TopicState::Retrying;
        const int generation = ++entry->retryGeneration;
        std::weak_ptr<bool> alive = this->aliveGuard_;
        QTimer::singleShot(entry->backoff.next(),
                           [this, alive, topic, generation] {
                               if (alive.expired())
                               {
                                   return;
                               }
                               auto *current = this->entryFor(topic);
                               if (current == nullptr ||
                                   current->retryGeneration != generation ||
                                   current->status.state != TopicState::Retrying)
                               {
                                   return;
                               }
                               this->sink_->relisten(
                                   topic, current->status.accountUserId);
                               current->status.state = TopicState::Pending;
                               this->diagChanged.invoke();
                           });
    }
    this->diagChanged.invoke();
}

void LimerinoPubSubController::onAuthSucceeded(const QString &tokenKey)
{
    this->authFailCounts_.erase(tokenKey);
}

void LimerinoPubSubController::onAuthFailed(const QString &tokenKey,
                                            const QString &error)
{
    const int count = ++this->authFailCounts_[tokenKey];
    if (count < this->config_.maxAuthFailures)
    {
        return;  // the connection's async authenticate may still heal
    }

    this->blockedAuthKeys_.insert(tokenKey);
    for (auto &[topic, entry] : this->entries_)
    {
        if (entry.status.auth == PubSubTopicAuth::User &&
            entry.status.accountUserId == tokenKey)
        {
            entry.retryGeneration++;
            entry.status.state = TopicState::Failed;
            entry.status.lastError = error;
        }
    }
    this->diagChanged.invoke();
}

void LimerinoPubSubController::onAuthUnavailable(const QString &tokenKey,
                                                 const QString &reason)
{
    for (auto &[topic, entry] : this->entries_)
    {
        if (entry.status.auth == PubSubTopicAuth::User &&
            entry.status.accountUserId == tokenKey &&
            entry.status.state != TopicState::AuthBlocked)
        {
            entry.retryGeneration++;
            entry.status.state = TopicState::Failed;
            entry.status.lastError = reason;
        }
    }
    this->diagChanged.invoke();
}

namespace {

// Topic prefix -> event category (drives the events channel's coloured chip).
// Longest prefix first; "event" is the fallback for unregistered topics.
QString eventCategoryFor(const QString &topic)
{
    static const std::pair<QString, QString> groups[] = {
        {QStringLiteral("chatrooms-user-v1."), QStringLiteral("moderation")},
        {QStringLiteral("community-points-user-v1."),
         QStringLiteral("points")},
        {QStringLiteral("predictions-user-v1."),
         QStringLiteral("prediction")},
        {QStringLiteral("predictions-channel-v1."),
         QStringLiteral("prediction")},
        {QStringLiteral("polls."), QStringLiteral("poll")},
        {QStringLiteral("raid."), QStringLiteral("raid")},
        {QStringLiteral("follows."), QStringLiteral("follow")},
    };
    for (const auto &[prefix, category] : groups)
    {
        if (topic.startsWith(prefix))
        {
            return category;
        }
    }
    return QStringLiteral("event");
}

}  // namespace

void LimerinoPubSubController::onTopicMessage(const QString &topic,
                                              const QJsonObject &payload)
{
    PubSubEvent event{
        .topic = topic,
        .channelId = hermesTopicSuffix(topic),
        .eventType = payload[QStringLiteral("type")].toString(),
        .category = eventCategoryFor(topic),
        .payload = payload,
        .displayText = {},
        .displayChannelId = {},
    };

    for (const auto &[prefix, handler] : this->handlers_)
    {
        if (topic.startsWith(prefix))
        {
            handler(topic, payload, event);
            break;
        }
    }

    if (event.displayText.isEmpty())
    {
        // Never print the raw topic string - it is an implementation detail.
        // Unknown events get an honestly-marked fallback line instead.
        event.displayText = event.eventType.isEmpty()
                                ? QStringLiteral("[unknown event]")
                                : QStringLiteral("[unhandled event: %1]")
                                      .arg(event.eventType);
    }

    if (!event.eventType.isEmpty())
    {
        this->registerKnownEventType(event.eventType);
    }

    // B4.1: drop byte-identical / heartbeat repeats (keyed on payload identity).
    // Skip when Settings is unavailable (unit tests construct bare controllers).
    if (Settings::hasInstance() &&
        getSettings()->limerinoPubSubDedupeEnabled.getValue())
    {
        const QString identity = pubSubEventIdentity(event);
        if (pubSubEventDedupe().isDuplicate(
                identity, pubSubDedupeWindowMs(event.eventType)))
        {
            return;
        }
    }

    // E1.b / B4.2: resolve displayChannelId -> login before constructing the
    // /events line. Unresolved ids are rewritten as "id:<n>" so they cannot be
    // mistaken for a display name.
    const QString channelId = event.displayChannelId;
    if (!channelId.isEmpty() && event.displayText.contains(channelId))
    {
        resolveChannelName(channelId, [this, event,
                                       channelId](const QString &name) mutable {
            if (name != channelId)
            {
                event.displayText.replace(channelId, name);
            }
            else
            {
                event.displayText.replace(
                    channelId, QStringLiteral("id:%1").arg(channelId));
            }
            this->eventProduced.invoke(event);
        });
        return;
    }

    this->eventProduced.invoke(event);
}

// ---- diagnostics / introspection ----

std::vector<LimerinoPubSubController::TopicStatus>
    LimerinoPubSubController::topicStatuses() const
{
    std::vector<TopicStatus> out;
    out.reserve(this->entries_.size());
    for (const auto &[topic, entry] : this->entries_)
    {
        out.push_back(entry.status);
    }
    return out;
}

LimerinoPubSubController::DiagSnapshot
    LimerinoPubSubController::diagSnapshot() const
{
    DiagSnapshot snapshot;
    snapshot.transport = this->sink_->transportStatus();
    for (const auto &[topic, entry] : this->entries_)
    {
        switch (entry.status.state)
        {
            case TopicState::Active:
                snapshot.topicsActive++;
                break;
            case TopicState::Pending:
                snapshot.topicsPending++;
                break;
            case TopicState::Retrying:
                snapshot.topicsRetrying++;
                break;
            case TopicState::Failed:
                snapshot.topicsFailed++;
                if (!entry.status.lastError.isEmpty())
                {
                    snapshot.lastErrorTopic = entry.status.topic;
                    snapshot.lastError = entry.status.lastError;
                }
                break;
            case TopicState::AuthBlocked:
                snapshot.topicsBlocked++;
                if (!entry.status.lastError.isEmpty())
                {
                    snapshot.lastErrorTopic = entry.status.topic;
                    snapshot.lastError = entry.status.lastError;
                }
                break;
        }
    }
    return snapshot;
}

void LimerinoPubSubController::registerTopicHandler(
    const QString &prefix, TopicMessageHandler handler)
{
    // Longest prefix first so specific topics win over generic ones.
    auto pos = std::find_if(
        this->handlers_.begin(), this->handlers_.end(),
        [&](const auto &existing) { return existing.first.size() < prefix.size(); });
    this->handlers_.insert(pos, {prefix, std::move(handler)});
}

void LimerinoPubSubController::registerKnownEventType(const QString &type)
{
    this->knownEventTypes_.insert(type);
}

QStringList LimerinoPubSubController::knownEventTypes() const
{
    QStringList out(this->knownEventTypes_.begin(), this->knownEventTypes_.end());
    out.sort(Qt::CaseInsensitive);
    return out;
}

// ---- bootstrap ----

LimerinoPubSubController *getPubSubController()
{
    assertInGuiThread();
    if (g_instance == nullptr)
    {
        initializePubSub();
    }
    return g_instance;
}

void initializePubSub()
{
    assertInGuiThread();
    if (g_instance != nullptr)
    {
        return;
    }

    auto manager = std::make_unique<HermesManager>();
    auto *managerPtr = manager.get();
    auto sink = std::make_unique<HermesManagerSink>(std::move(manager));

    g_instance = new LimerinoPubSubController(std::move(sink),
                                              &resolveWithLimerinoAuth,
                                              LimerinoPubSubController::Config{});

    // Topic-specific parsers/handlers (batch P1+), registered once.
    installHermesChannelTopicHandlers(*g_instance);
    installHermesUserTopicHandlers(*g_instance);

    // Connections authenticate with the token live at authenticate time.
    managerPtr->setAuthResolver(
        [](const QString &key, QString *reason) -> std::optional<QString> {
            return getPubSubController()->resolveLiveToken(key, reason);
        });

    // One-shot startup validation (deferred 30 s inside LimerinoAuth).
    LimerinoAuth::scheduleStartupRefresh();

    // Process-lifetime singletons: intentionally never destroyed (avoids
    // shutdown-order hazards against the websocket pool).
}

}  // namespace chatterino::limerino
