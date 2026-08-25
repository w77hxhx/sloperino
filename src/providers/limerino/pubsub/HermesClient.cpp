// SPDX-License-Identifier: MIT

#include "providers/limerino/pubsub/HermesClient.hpp"

#include "common/QLogging.hpp"
#include "debug/AssertInGuiThread.hpp"
#include "providers/limerino/pubsub/HermesManager.hpp"
#include "providers/limerino/pubsub/HermesMessages.hpp"
#include "util/PostToThread.hpp"

namespace chatterino::limerino {

using namespace Qt::Literals;

// client.js L30 (transcribed; also governs MAX_CONNECTIONS behaviour upstream
// in the pool manager's pending queue).
static_assert(HermesClient::MAX_SUBSCRIPTIONS == 100);

QDebug operator<<(QDebug debug, const HermesSubscription &data)
{
    QDebugStateSaver saver(debug);
    debug.nospace() << "HermesSubscription(" << data.topic << ", key="
                    << (data.tokenKey.isEmpty() ? u"<none>"_s : data.tokenKey)
                    << ')';
    return debug;
}

HermesClient::HermesClient(HermesManager &manager)
    : manager_(manager)
{
    this->keepaliveTimer_.setInterval(2000);  // HEALTH_CHECK_INTERVAL_MS (L29)
    QObject::connect(&this->keepaliveTimer_, &QTimer::timeout,
                     [this] { this->checkKeepalive(); });
}

HermesClient::~HermesClient() = default;

void HermesClient::onOpen()
{
    assertInGuiThread();
    this->open_ = true;
    this->authState_ = AuthState::Idle;
    this->driveWire();
}

void HermesClient::onMessage(const QByteArray &msg)
{
    // Called from the websocket thread - marshal to the GUI thread.
    runInGuiThread([weak = this->weak_from_this(), msg] {
        auto self = weak.lock();
        if (self)
        {
            self->handleMessage(msg);
        }
    });
}

void HermesClient::close()
{
    this->ws_.close();
}

bool HermesClient::isOpen() const
{
    return this->open_;
}

bool HermesClient::isSubscribed(const Subscription &sub) const
{
    return this->subscriptions_.contains(sub);
}

bool HermesClient::canAccept(const QString &tokenKey) const
{
    if (!this->identityKey_.has_value())
    {
        return true;
    }
    if (this->identityKey_->isEmpty())
    {
        // An unauthenticated connection carries unauthenticated topics only.
        return tokenKey.isEmpty();
    }
    // An authenticated connection carries its own topics plus topics that
    // need no token (client.js rides CHANNEL_SUBS on the authed connection).
    return tokenKey.isEmpty() || tokenKey == *this->identityKey_;
}

bool HermesClient::subscribe(const Subscription &sub)
{
    assertInGuiThread();

    if (this->subscriptions_.size() >= this->maxSubscriptions)
    {
        return false;
    }
    if (this->subscriptions_.contains(sub))
    {
        return true;  // already accepted
    }
    if (!this->canAccept(sub.tokenKey))
    {
        return false;
    }

    if (!this->identityKey_.has_value())
    {
        this->identityKey_ = sub.tokenKey;
    }
    this->subscriptions_.emplace(sub);
    this->queued_.push_back(sub);
    qCDebug(chatterinoLiveupdates) << "Hermes: accepted" << sub;
    this->driveWire();
    return true;
}

bool HermesClient::unsubscribe(const Subscription &sub)
{
    assertInGuiThread();

    if (this->subscriptions_.erase(sub) <= 0)
    {
        return false;
    }

    std::erase_if(this->queued_, [&](const Subscription &queued) {
        return queued == sub;
    });

    auto idIt = this->topicToId_.find(sub.topic);
    if (idIt != this->topicToId_.end())
    {
        if (this->open_)
        {
            // unsubscribes reuse the subscription's id (client.js L258-264)
            this->ws_.sendText(
                makeHermesUnsubscribeMessage(idIt->second, sub.topic));
        }
        this->idToTopic_.erase(idIt->second);
        this->topicToId_.erase(idIt);
    }
    return true;
}

void HermesClient::retryAuthentication()
{
    assertInGuiThread();

    if (this->authState_ == AuthState::Unavailable ||
        this->authState_ == AuthState::Failed)
    {
        this->authState_ = AuthState::Idle;
        this->driveWire();
    }
}

void HermesClient::driveWire()
{
    if (!this->open_)
    {
        return;
    }
    switch (this->authState_)
    {
        case AuthState::Idle:
            if (!this->queued_.empty())
            {
                this->beginAuthentication();
            }
            break;
        case AuthState::Unauthenticated:
        case AuthState::Authenticated:
            this->flushQueued();
            break;
        default:
            // Authenticating: flush on ok. Unavailable/Failed: stay queued.
            break;
    }
}

void HermesClient::beginAuthentication()
{
    const QString key = this->identityKey_.value_or(QString());

    QString reason;
    auto token = this->manager_.resolveToken(key, &reason);
    if (token.has_value() && !token->isEmpty())
    {
        this->authState_ = AuthState::Authenticating;
        // Never logged: the frame carries the token.
        this->ws_.sendText(makeHermesAuthenticateMessage(*token));
        return;
    }

    if (key.isEmpty())
    {
        // Unauthenticated topics and no token anywhere: send raw subscribes
        // (unknown server tolerance - surfaced via subscribe failures if any).
        qCDebug(chatterinoLiveupdates)
            << "Hermes: no token available, subscribing unauthenticated";
        this->authState_ = AuthState::Unauthenticated;
        this->flushQueued();
        return;
    }

    this->authState_ = AuthState::Unavailable;
    this->manager_.clientAuthUnavailable(this, key, reason);
}

void HermesClient::flushQueued()
{
    for (const auto &sub : this->queued_)
    {
        this->sendSubscribe(sub);
    }
    this->queued_.clear();
}

void HermesClient::sendSubscribe(const Subscription &sub)
{
    const QString id = hermesRandomId();
    this->topicToId_[sub.topic] = id;
    this->idToTopic_[id] = sub.topic;
    this->ws_.sendText(makeHermesSubscribeMessage(id, sub.topic));
}

void HermesClient::handleMessage(const QByteArray &msg)
{
    assertInGuiThread();
    this->manager_.hermesDiag.messagesReceived++;

    auto frame = parseHermesFrame(msg);
    if (!frame || frame->type == HermesFrame::Type::INVALID)
    {
        qCDebug(chatterinoLiveupdates)
            << "Hermes: unparseable/unknown frame type:"
            << (frame ? frame->typeString : u"<invalid json>"_s);
        this->manager_.hermesDiag.messagesFailedToParse++;
        return;
    }

    switch (frame->type)
    {
        case HermesFrame::Type::Welcome:
            this->onWelcome(frame->object);
            break;
        case HermesFrame::Type::Keepalive:
            this->lastKeepaliveAt_ = QDateTime::currentDateTimeUtc();
            break;
        case HermesFrame::Type::Reconnect:
            // client.js L372-376: server-requested reconnect -> close
            this->manager_.clientReconnectRequested();
            this->close();
            break;
        case HermesFrame::Type::AuthenticateResponse:
            this->onAuthenticateResponse(frame->object);
            break;
        case HermesFrame::Type::SubscribeResponse:
            this->onSubscribeResponse(frame->object);
            break;
        case HermesFrame::Type::UnsubscribeResponse:
            this->onUnsubscribeResponse(frame->object);
            break;
        case HermesFrame::Type::Notification:
            this->onNotification(frame->object);
            break;
        default:
            break;
    }
}

void HermesClient::onWelcome(const QJsonObject &object)
{
    auto welcome = parseHermesWelcome(object);
    if (!welcome)
    {
        return;
    }
    // client.js L356: (msg.welcome.keepaliveSec || 10) + 2.5
    this->keepaliveMs_ =
        std::chrono::milliseconds(static_cast<qint64>(welcome->keepaliveSec * 1000) + 2500);
    this->lastKeepaliveAt_ = QDateTime::currentDateTimeUtc();
    this->keepaliveTimer_.start();
}

void HermesClient::onAuthenticateResponse(const QJsonObject &object)
{
    auto response = parseHermesAuthenticateResponse(object);
    if (!response)
    {
        return;
    }

    const QString key = this->identityKey_.value_or(QString());
    if (response->result == "ok"_L1)
    {
        this->authState_ = AuthState::Authenticated;
        this->manager_.clientAuthSucceeded(this, key);
        this->flushQueued();
        return;
    }

    // client.js L379-387: log + close
    this->authState_ = AuthState::Failed;
    this->manager_.clientAuthFailed(
        this, key,
        response->error.isEmpty() ? response->errorCode : response->error);
    this->close();
}

void HermesClient::onSubscribeResponse(const QJsonObject &object)
{
    auto parentId = hermesResponseParentId(object);
    auto response = parseHermesSubscribeResponse(object);
    if (!parentId || !response)
    {
        return;
    }

    auto topicIt = this->idToTopic_.find(*parentId);
    if (topicIt == this->idToTopic_.end())
    {
        qCDebug(chatterinoLiveupdates)
            << "Hermes: subscribeResponse for unknown id";
        return;
    }
    const QString topic = topicIt->second;

    this->manager_.hermesDiag.subscribeResponses++;
    if (response->result == "ok"_L1)
    {
        // Keep id <-> topic: notifications route by subscription id.
        this->manager_.clientSubscribeSucceeded(this, topic);
        return;
    }

    this->manager_.hermesDiag.failedSubscribeResponses++;
    // Any error (including "too many subscriptions", client.js L414-425) is
    // reported uniformly; the controller owns retry/backoff/cap/terminal.
    this->idToTopic_.erase(topicIt);
    this->topicToId_.erase(topic);

    const QString error =
        response->error.isEmpty() ? response->errorCode : response->error;
    this->manager_.clientSubscribeFailed(this, topic,
                                         error.isEmpty() ? u"unknown"_s : error);
}

void HermesClient::onUnsubscribeResponse(const QJsonObject &object)
{
    // We already dropped local state in unsubscribe(); this only logs.
    std::ignore = hermesResponseParentId(object);
    std::ignore = parseHermesUnsubscribeResponse(object);
}

void HermesClient::onNotification(const QJsonObject &object)
{
    this->manager_.hermesDiag.notificationsReceived++;

    auto notification = parseHermesNotification(object);
    if (!notification)
    {
        qCDebug(chatterinoLiveupdates) << "Hermes: malformed notification";
        this->manager_.hermesDiag.messagesFailedToParse++;
        return;
    }

    const auto topicIt = this->idToTopic_.find(notification->subscriptionId);
    if (topicIt == this->idToTopic_.end())
    {
        qCDebug(chatterinoLiveupdates)
            << "Hermes: notification for unknown subscription id";
        return;
    }

    this->manager_.clientTopicMessage(topicIt->second,
                                      notification->payload);
}

void HermesClient::checkKeepalive()
{
    if (!this->open_)
    {
        return;
    }
    if (this->lastKeepaliveAt_.isValid() &&
        this->lastKeepaliveAt_.msecsTo(QDateTime::currentDateTimeUtc()) >
            this->keepaliveMs_.count())
    {
        // client.js L360-365: missed keepalive -> close
        this->manager_.clientMissedKeepalive();
        this->keepaliveTimer_.stop();
        this->close();
    }
}

}  // namespace chatterino::limerino
