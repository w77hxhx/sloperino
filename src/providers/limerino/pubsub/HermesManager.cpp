// SPDX-License-Identifier: MIT

#include "providers/limerino/pubsub/HermesManager.hpp"

#include "common/QLogging.hpp"
#include "debug/AssertInGuiThread.hpp"
#include "providers/limerino/LimerinoAuth.hpp"

namespace chatterino::limerino {

using namespace Qt::Literals;

HermesManager::HermesManager()
    // client.js L25: wss://hermes.twitch.tv/v1?clientId=<id>
    // The client id pairs with the LimerinoAuth-issued tokens we authenticate
    // with (never the default Chatterino one).
    : BasicPubSubManager(
          u"wss://hermes.twitch.tv/v1?clientId="_s + LimerinoAuth::CLIENT_ID,
          u"Hermes"_s)
{
}

HermesManager::~HermesManager()
{
    this->stop();
}

std::shared_ptr<HermesClient> HermesManager::makeClient()
{
    return std::make_shared<HermesClient>(*this);
}

void HermesManager::listen(const QString &topic, const QString &tokenKey)
{
    assertInGuiThread();
    qCDebug(chatterinoLiveupdates) << "Hermes: subscribe" << topic
                                   << "key:" << (tokenKey.isEmpty()
                                                     ? u"<none>"_s : tokenKey);
    this->subscribe(HermesSubscription{topic, tokenKey});
}

void HermesManager::unlisten(const QString &topic)
{
    assertInGuiThread();
    qCDebug(chatterinoLiveupdates) << "Hermes: unsubscribe" << topic;
    this->unsubscribe(HermesSubscription{topic, {}});
}

void HermesManager::relisten(const QString &topic, const QString &tokenKey)
{
    assertInGuiThread();
    this->unlisten(topic);
    this->listen(topic, tokenKey);
}

void HermesManager::reconnect()
{
    assertInGuiThread();
    for (const auto &[id, client] : this->clients())
    {
        client->close();
    }
}

void HermesManager::setAuthResolver(AuthResolver resolver)
{
    this->authResolver_ = std::move(resolver);
}

std::optional<QString> HermesManager::resolveToken(const QString &tokenKey,
                                                   QString *reason)
{
    if (!this->authResolver_)
    {
        if (reason != nullptr)
        {
            *reason = u"live-updates auth is not initialized"_s;
        }
        return std::nullopt;
    }
    return this->authResolver_(tokenKey, reason);
}

std::unordered_map<QString, QString> HermesManager::subscribedTopics() const
{
    std::unordered_map<QString, QString> out;
    for (const auto &[id, client] : this->clients())
    {
        for (const auto &sub : client->subscriptions_)
        {
            out.emplace(sub.topic, sub.tokenKey);
        }
    }
    return out;
}

size_t HermesManager::connectionCount() const
{
    return this->clients().size();
}

void HermesManager::retryAuthentication(const QString &tokenKey)
{
    for (const auto &[id, client] : this->clients())
    {
        if (client->identityKey_.has_value() &&
            *client->identityKey_ == tokenKey)
        {
            client->retryAuthentication();
        }
    }
}

const liveupdates::Diag &HermesManager::wsDiag() const
{
    return this->diag;
}

void HermesManager::clientAuthSucceeded(HermesClient * /*client*/,
                                        const QString &tokenKey)
{
    this->authSucceeded.invoke(tokenKey, QString());
}

void HermesManager::clientAuthFailed(HermesClient * /*client*/,
                                     const QString &tokenKey,
                                     const QString &error)
{
    qCDebug(chatterinoLiveupdates)
        << "Hermes: authenticate failed, key:" << tokenKey << "error:" << error;
    this->hermesDiag.authFailures++;
    this->authFailed.invoke(tokenKey, error);
}

void HermesManager::clientAuthUnavailable(HermesClient * /*client*/,
                                          const QString &tokenKey,
                                          const QString &reason)
{
    this->authUnavailable.invoke(tokenKey, reason);
}

void HermesManager::clientSubscribeSucceeded(HermesClient * /*client*/,
                                             const QString &topic)
{
    this->subscribeSucceeded.invoke(topic);
}

void HermesManager::clientSubscribeFailed(HermesClient * /*client*/,
                                          const QString &topic,
                                          const QString &error)
{
    qCDebug(chatterinoLiveupdates)
        << "Hermes: subscribe failed:" << topic << "error:" << error;
    this->subscribeFailed.invoke(topic, error);
}

void HermesManager::clientTopicMessage(const QString &topic,
                                       const QJsonObject &payload)
{
    this->topicMessage.invoke(topic, payload);
}

void HermesManager::clientReconnectRequested()
{
    this->hermesDiag.reconnectsReceived++;
}

void HermesManager::clientMissedKeepalive()
{
    this->hermesDiag.keepalivesMissed++;
}

}  // namespace chatterino::limerino
