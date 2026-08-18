// SPDX-FileCopyrightText: 2026 Contributors to Chatterino <https://chatterino.com>
//
// SPDX-License-Identifier: MIT

#include "providers/firehose/FirehoseManager.hpp"

#include "Application.hpp"
#include "common/QLogging.hpp"
#include "messages/Message.hpp"
#include "messages/MessageBuilder.hpp"
#include "providers/firehose/StalkChannel.hpp"
#include "providers/twitch/IrcMessageHandler.hpp"
#include "providers/twitch/TwitchHelpers.hpp"
#include "providers/twitch/TwitchIrcServer.hpp"
#include "singletons/Settings.hpp"
#include "util/Helpers.hpp"

#include <IrcCommand>
#include <IrcMessage>
#include <QJsonArray>
#include <QJsonDocument>
#include <QJsonObject>
#include <QRegularExpression>

#include <algorithm>
#include <chrono>

namespace chatterino {

class FirehoseWsListener final : public WebSocketListener
{
public:
    FirehoseWsListener(FirehoseManager *mgr, size_t index)
        : mgr_(mgr)
        , index_(index)
    {
    }

    void onOpen() override
    {
        QMetaObject::invokeMethod(
            this->mgr_,
            [mgr = this->mgr_, idx = this->index_] {
                mgr->onEndpointConnected(idx);
            },
            Qt::QueuedConnection);
    }

    void onTextMessage(QByteArray data) override
    {
        this->mgr_->onRawMessageReceived(std::move(data));
    }

    void onBinaryMessage(QByteArray data) override
    {
        this->mgr_->onRawMessageReceived(std::move(data));
    }

    void onClose(std::unique_ptr<WebSocketListener> /*self*/) override
    {
        QMetaObject::invokeMethod(
            this->mgr_,
            [mgr = this->mgr_, idx = this->index_] {
                mgr->scheduleReconnect(idx);
            },
            Qt::QueuedConnection);
    }

private:
    FirehoseManager *mgr_;
    size_t index_;
};

FirehoseManager::FirehoseManager()
    : channel_(std::make_shared<FirehoseChannel>())
{
    this->initEndpoints();

    // 1-second statistics timer
    this->statsTimer_.setTimerType(Qt::PreciseTimer);
    this->statsTimer_.setInterval(1000);
    QObject::connect(&this->statsTimer_, &QTimer::timeout, this,
                     &FirehoseManager::updateStats);

    // 15-second watchdog timer to keep connection throughput resilient
    this->watchdogTimer_.setTimerType(Qt::CoarseTimer);
    this->watchdogTimer_.setInterval(15000);
    QObject::connect(&this->watchdogTimer_, &QTimer::timeout, this,
                     &FirehoseManager::runWatchdog);
}

FirehoseManager::~FirehoseManager()
{
    this->statsTimer_.stop();
    this->watchdogTimer_.stop();
    for (size_t i = 0; i < this->endpoints_.size(); ++i)
    {
        this->disconnectEndpoint(i);
    }
}

std::shared_ptr<FirehoseChannel> FirehoseManager::getChannel() const
{
    return this->channel_;
}

bool FirehoseManager::isNeeded() const
{
    return this->firehoseAttachedCount_ > 0 || !this->stalkChannels_.empty();
}

void FirehoseManager::checkConnectionState()
{
    if (this->isNeeded())
    {
        if (!this->isRunning_)
        {
            this->isRunning_ = true;
            this->initialize();
            this->statsTimer_.start();
            this->watchdogTimer_.start();
        }
    }
    else
    {
        if (this->isRunning_)
        {
            this->isRunning_ = false;
            this->statsTimer_.stop();
            this->watchdogTimer_.stop();
            for (size_t i = 0; i < this->endpoints_.size(); ++i)
            {
                this->disconnectEndpoint(i);
            }
        }
    }
}

void FirehoseManager::addFirehoseConsumer()
{
    this->firehoseAttachedCount_++;
    this->checkConnectionState();
}

void FirehoseManager::removeFirehoseConsumer()
{
    if (this->firehoseAttachedCount_ > 0)
    {
        this->firehoseAttachedCount_--;
    }
    this->checkConnectionState();
}

void FirehoseManager::initEndpoints()
{
    auto &s = *getSettings();

    this->endpoints_.clear();

    auto addEndpoint = [this](QString name, QUrl url, BoolSetting *setting) {
        Endpoint ep;
        ep.name = std::move(name);
        ep.url = std::move(url);
        ep.enabledSetting = setting;
        this->endpoints_.push_back(std::move(ep));
    };

    addEndpoint(QStringLiteral("Spanix"),
                QUrl(QStringLiteral("wss://logs.spanix.team/firehose")),
                &s.firehoseEnableSpanix);
    addEndpoint(QStringLiteral("Supa"),
                QUrl(QStringLiteral("wss://logs.supa.codes/firehose")),
                &s.firehoseEnableSupa);
    addEndpoint(QStringLiteral("Susgee"),
                QUrl(QStringLiteral("wss://logs.susgee.dev/firehose")),
                &s.firehoseEnableSusgee);
    addEndpoint(QStringLiteral("Nadeko"),
                QUrl(QStringLiteral("wss://logs.nadeko.net/firehose")),
                &s.firehoseEnableNadeko);
    addEndpoint(QStringLiteral("Logxx"),
                QUrl(QStringLiteral("wss://logxx.dev/firehose")),
                &s.firehoseEnableLogxx);
    addEndpoint(QStringLiteral("Catquery"),
                QUrl(QStringLiteral("wss://firehose.catquery.com")),
                &s.firehoseEnableCatquery);

    for (size_t i = 0; i < this->endpoints_.size(); ++i)
    {
        auto &ep = this->endpoints_[i];
        ep.reconnectTimer = std::make_unique<QTimer>();
        ep.reconnectTimer->setSingleShot(true);
        QObject::connect(ep.reconnectTimer.get(), &QTimer::timeout, this,
                         [this, i] {
                             if (this->isNeeded())
                             {
                                 this->connectEndpoint(i);
                             }
                         });

        if (ep.enabledSetting)
        {
            ep.enabledSetting->connect(
                [this, i](bool enabled) {
                    if (enabled && this->isNeeded())
                    {
                        this->connectEndpoint(i);
                    }
                    else
                    {
                        this->disconnectEndpoint(i);
                    }
                },
                this->signalHolder_);
        }
    }
}

void FirehoseManager::initialize()
{
    if (!this->isNeeded())
    {
        return;
    }

    for (size_t i = 0; i < this->endpoints_.size(); ++i)
    {
        auto &ep = this->endpoints_[i];
        if (ep.enabledSetting && ep.enabledSetting->getValue())
        {
            this->connectEndpoint(i);
        }
    }
}

void FirehoseManager::reconnectAll()
{
    if (!this->isNeeded())
    {
        return;
    }

    for (size_t i = 0; i < this->endpoints_.size(); ++i)
    {
        this->disconnectEndpoint(i);
        if (this->endpoints_[i].enabledSetting &&
            this->endpoints_[i].enabledSetting->getValue())
        {
            this->connectEndpoint(i);
        }
    }
}

void FirehoseManager::connectEndpoint(size_t index)
{
    if (index >= this->endpoints_.size())
    {
        return;
    }

    auto &ep = this->endpoints_[index];
    if (ep.enabledSetting && !ep.enabledSetting->getValue())
    {
        return;
    }

    ep.reconnectTimer->stop();

    WebSocketOptions options{
        .url = ep.url,
        .headers = {},
    };

    auto listener = std::make_unique<FirehoseWsListener>(this, index);
    ep.handle =
        this->wsPool_.createSocket(std::move(options), std::move(listener));
}

void FirehoseManager::onEndpointConnected(size_t index)
{
    if (index >= this->endpoints_.size())
    {
        return;
    }

    auto &ep = this->endpoints_[index];
    ep.isConnected = true;
    ep.reconnectBackoffMs = 2000;
}

void FirehoseManager::disconnectEndpoint(size_t index)
{
    if (index >= this->endpoints_.size())
    {
        return;
    }

    auto &ep = this->endpoints_[index];
    ep.reconnectTimer->stop();
    ep.handle.close();
    ep.isConnected = false;
    ep.reconnectBackoffMs = 2000;
}

void FirehoseManager::scheduleReconnect(size_t index)
{
    if (index >= this->endpoints_.size())
    {
        return;
    }

    auto &ep = this->endpoints_[index];
    ep.isConnected = false;

    if (!getSettings()->firehoseAutoReconnect.getValue())
    {
        return;
    }

    if (ep.enabledSetting && !ep.enabledSetting->getValue())
    {
        return;
    }

    int delay = ep.reconnectBackoffMs;
    ep.reconnectBackoffMs = std::min(ep.reconnectBackoffMs * 2, 15000);

    ep.reconnectTimer->start(delay);
}

void FirehoseManager::onRawMessageReceived(QByteArray data)
{
    if (!this->isNeeded() || data.isEmpty())
    {
        return;
    }

    const char *raw = data.constData();
    const int len = data.size();

    int start = 0;
    std::vector<MessagePtr> batch;

    while (start < len)
    {
        int next = data.indexOf('\n', start);
        if (next == -1)
        {
            next = len;
        }
        int lineLen = next - start;
        if (lineLen > 0 && raw[start + lineLen - 1] == '\r')
        {
            lineLen--;
        }
        if (lineLen > 0)
        {
            QByteArray line = data.mid(start, lineLen);
            uint64_t rawHash = 0;
            if (extractRawMessageHash(line, rawHash))
            {
                if (this->seenHashes_.find(rawHash) != this->seenHashes_.end())
                {
                    start = next + 1;
                    continue;  // Duplicate across multiple firehose endpoints
                }

                this->seenHashes_.insert(rawHash);
                this->seenQueue_.push_back(rawHash);

                if (this->seenQueue_.size() > MAX_DEDUP_CACHE_SIZE)
                {
                    this->seenHashes_.erase(this->seenQueue_.front());
                    this->seenQueue_.pop_front();
                }
            }

            QString msgId;
            auto msg = this->parseRawPayload(line, msgId);
            if (msg)
            {
                this->messagesThisSecond_++;

                // Route message to active stalk channels matching target username
                if (!this->stalkChannels_.empty())
                {
                    for (auto it = this->stalkChannels_.begin();
                         it != this->stalkChannels_.end();)
                    {
                        if (auto stalk = it->lock())
                        {
                            const auto &target = stalk->targetUser();
                            if (!target.isEmpty() &&
                                (msg->loginName.compare(
                                     target, Qt::CaseInsensitive) == 0 ||
                                 msg->displayName.compare(
                                     target, Qt::CaseInsensitive) == 0))
                            {
                                stalk->addMessage(msg,
                                                  MessageContext::Original);
                            }
                            ++it;
                        }
                        else
                        {
                            it = this->stalkChannels_.erase(it);
                        }
                    }
                }

                // Route message to global /mentions channel without duplicate sound
                if (msg->flags.has(MessageFlag::Highlighted) &&
                    msg->flags.has(MessageFlag::ShowInMentions))
                {
                    if (auto mentions =
                            getApp()->getTwitch()->getMentionsChannel())
                    {
                        mentions->addMessage(msg, MessageContext::Original);
                    }
                }

                batch.emplace_back(std::move(msg));
            }
        }
        start = next + 1;
    }

    if (!batch.empty() && this->channel_)
    {
        this->channel_->addMessagesBatch(batch);
    }
}

void FirehoseManager::runWatchdog()
{
    if (!this->isNeeded())
    {
        return;
    }

    for (size_t i = 0; i < this->endpoints_.size(); ++i)
    {
        auto &ep = this->endpoints_[i];
        if (ep.enabledSetting && ep.enabledSetting->getValue())
        {
            if (!ep.isConnected && !ep.reconnectTimer->isActive())
            {
                this->connectEndpoint(i);
            }
        }
    }
}

void FirehoseManager::registerStalkChannel(
    const std::shared_ptr<StalkChannel> &channel)
{
    if (!channel)
    {
        return;
    }

    for (const auto &w : this->stalkChannels_)
    {
        if (auto locked = w.lock())
        {
            if (locked == channel)
            {
                return;
            }
        }
    }

    this->stalkChannels_.push_back(channel);
    this->checkConnectionState();
}

void FirehoseManager::unregisterStalkChannel(
    const std::shared_ptr<StalkChannel> &channel)
{
    if (!channel)
    {
        return;
    }

    for (auto it = this->stalkChannels_.begin();
         it != this->stalkChannels_.end();)
    {
        if (auto locked = it->lock())
        {
            if (locked == channel)
            {
                it = this->stalkChannels_.erase(it);
                break;
            }
            ++it;
        }
        else
        {
            it = this->stalkChannels_.erase(it);
        }
    }
    this->checkConnectionState();
}

void FirehoseManager::updateStats()
{
    this->currentMsgPerSecond_ = this->messagesThisSecond_;
    this->messagesThisSecond_ = 0;

    int activeCount = 0;
    int totalCount = static_cast<int>(this->endpoints_.size());
    for (const auto &ep : this->endpoints_)
    {
        if (ep.isConnected)
        {
            activeCount++;
        }
    }

    this->channel_->updateStatus(this->currentMsgPerSecond_, activeCount,
                                 totalCount);
}

MessagePtr FirehoseManager::parseRawPayload(const QByteArray &data,
                                            QString &outMsgId)
{
    const char *trimmed = data.constData();
    while (*trimmed == ' ' || *trimmed == '\t' || *trimmed == '\r' ||
           *trimmed == '\n')
    {
        trimmed++;
    }

    if (*trimmed == '{')
    {
        return this->parseJsonPayload(data, outMsgId);
    }
    else
    {
        return this->parseIrcLine(data, outMsgId);
    }
}

MessagePtr FirehoseManager::parseIrcLine(const QByteArray &data,
                                         QString &outMsgId)
{
    auto *ircMsg = Communi::IrcMessage::fromData(data, nullptr);
    if (!ircMsg)
    {
        return nullptr;
    }

    auto tags = ircMsg->tags();
    outMsgId = tags.getOrEmpty("id");

    if (ircMsg->command() != QStringLiteral("PRIVMSG"))
    {
        delete ircMsg;
        return nullptr;
    }

    auto *privMsg = dynamic_cast<Communi::IrcPrivateMessage *>(ircMsg);
    if (!privMsg)
    {
        delete ircMsg;
        return nullptr;
    }

    QString targetChan;
    trimChannelName(privMsg->target(), targetChan);

    auto twitchChan = getApp()->getTwitch()->getChannelOrEmpty(targetChan);
    Channel *targetChannel = twitchChan.get();
    if (!targetChannel || targetChannel->isEmpty())
    {
        if (this->fallbackChannels_.size() > 1000)
        {
            this->fallbackChannels_.clear();
        }
        auto &cached = this->fallbackChannels_[targetChan];
        if (!cached)
        {
            cached =
                std::make_shared<Channel>(targetChan, Channel::Type::Twitch);
        }
        targetChannel = cached.get();
    }

    MessageParseArgs args;
    args.isAction = privMsg->isAction();
    args.skipClientDetection = true;

    QString content = unescapeZeroWidthJoiner(privMsg->content());
    int messageOffset = stripLeadingReplyMention(tags, content);

    auto [builtMsg, alert] = MessageBuilder::makeIrcMessage(
        targetChannel, privMsg, args, content, messageOffset);

    delete ircMsg;

    return builtMsg;
}

MessagePtr FirehoseManager::parseJsonPayload(const QByteArray &data,
                                             QString &outMsgId)
{
    QJsonParseError err{};
    auto doc = QJsonDocument::fromJson(data, &err);
    if (err.error != QJsonParseError::NoError || !doc.isObject())
    {
        return nullptr;
    }

    auto root = doc.object();
    QString text = root.value(QStringLiteral("text")).toString();
    QString username = root.value(QStringLiteral("username")).toString();
    QString displayName = root.value(QStringLiteral("displayName")).toString();
    QString channel = root.value(QStringLiteral("channel")).toString();
    outMsgId = root.value(QStringLiteral("id")).toString();

    auto tagsObj = root.value(QStringLiteral("tags")).toObject();

    if (outMsgId.isEmpty())
    {
        outMsgId = tagsObj.value(QStringLiteral("id")).toString();
    }

    // Build synthetic IRC PRIVMSG with tags to use Chatterino's full rendering pipeline
    QString tagStr;
    for (auto it = tagsObj.begin(); it != tagsObj.end(); ++it)
    {
        if (!tagStr.isEmpty())
        {
            tagStr += QLatin1Char(';');
        }
        tagStr +=
            it.key() + QLatin1Char('=') + it.value().toVariant().toString();
    }

    if (!tagsObj.contains(QStringLiteral("display-name")) &&
        !displayName.isEmpty())
    {
        if (!tagStr.isEmpty())
        {
            tagStr += QLatin1Char(';');
        }
        tagStr += QStringLiteral("display-name=") + displayName;
    }

    if (!tagsObj.contains(QStringLiteral("id")) && !outMsgId.isEmpty())
    {
        if (!tagStr.isEmpty())
        {
            tagStr += QLatin1Char(';');
        }
        tagStr += QStringLiteral("id=") + outMsgId;
    }

    QString rawIrc =
        QStringLiteral("@%1 :%2!%2@%2.tmi.twitch.tv PRIVMSG #%3 :%4\r\n")
            .arg(tagStr, username, channel, text);

    return this->parseIrcLine(rawIrc.toUtf8(), outMsgId);
}

}  // namespace chatterino
