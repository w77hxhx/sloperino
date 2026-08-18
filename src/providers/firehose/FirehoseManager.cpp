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

    // 16ms batch processing timer (≈60fps-like throughput)
    this->batchTimer_.setInterval(
        getSettings()->firehoseBatchIntervalMs.getValue());
    QObject::connect(&this->batchTimer_, &QTimer::timeout, this,
                     &FirehoseManager::processBatch);
    this->batchTimer_.start();

    getSettings()->firehoseBatchIntervalMs.connect(
        [this](int ms) {
            this->batchTimer_.setInterval(std::clamp(ms, 10, 2000));
        },
        this->signalHolder_);

    // 1-second statistics timer
    this->statsTimer_.setInterval(1000);
    QObject::connect(&this->statsTimer_, &QTimer::timeout, this,
                     &FirehoseManager::updateStats);
    this->statsTimer_.start();
}

FirehoseManager::~FirehoseManager()
{
    this->batchTimer_.stop();
    this->statsTimer_.stop();
    for (size_t i = 0; i < this->endpoints_.size(); ++i)
    {
        this->disconnectEndpoint(i);
    }
}

std::shared_ptr<FirehoseChannel> FirehoseManager::getChannel() const
{
    return this->channel_;
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
                             this->connectEndpoint(i);
                         });

        if (ep.enabledSetting)
        {
            ep.enabledSetting->connect(
                [this, i](bool enabled) {
                    if (enabled)
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
    if (data.isEmpty())
    {
        return;
    }

    std::lock_guard<std::mutex> lock(this->queueMutex_);

    if (data.contains('\n'))
    {
        int start = 0;
        int size = data.size();
        while (start < size)
        {
            int next = data.indexOf('\n', start);
            if (next == -1)
            {
                next = size;
            }
            int len = next - start;
            if (len > 0 && data[start + len - 1] == '\r')
            {
                len--;
            }
            if (len > 0)
            {
                this->rawQueue_.emplace_back(data.mid(start, len));
            }
            start = next + 1;
        }
    }
    else
    {
        this->rawQueue_.emplace_back(std::move(data));
    }
}

static inline uint64_t hashBytes64(const char *data, size_t len) noexcept
{
    uint64_t hash = 14695981039346656037ull;
    for (size_t i = 0; i < len; ++i)
    {
        hash ^= static_cast<uint8_t>(data[i]);
        hash *= 1099511628211ull;
    }
    return hash;
}

static bool extractRawMessageHash(const QByteArray &data, uint64_t &outHash)
{
    const char *ptr = data.constData();
    const char *end = ptr + data.size();

    const char *trimmed = ptr;
    while (trimmed < end && (*trimmed == ' ' || *trimmed == '\t' ||
                             *trimmed == '\r' || *trimmed == '\n'))
    {
        trimmed++;
    }

    if (trimmed >= end)
    {
        return false;
    }

    if (*trimmed == '{')
    {
        int idPos = data.indexOf("\"id\":");
        if (idPos != -1)
        {
            int start = data.indexOf('"', idPos + 5);
            if (start != -1)
            {
                int finish = data.indexOf('"', start + 1);
                if (finish != -1 && finish > start)
                {
                    outHash = hashBytes64(data.constData() + start + 1,
                                          finish - start - 1);
                    return true;
                }
            }
        }
        return false;
    }

    if (*trimmed == '@')
    {
        int idPos = data.indexOf("id=");
        if (idPos != -1 &&
            (idPos == 1 || data[idPos - 1] == ';' || data[idPos - 1] == '@'))
        {
            int start = idPos + 3;
            int finish = data.indexOf(';', start);
            int space = data.indexOf(' ', start);
            if (finish == -1 || (space != -1 && space < finish))
            {
                finish = space;
            }
            if (finish != -1 && finish > start)
            {
                outHash = hashBytes64(data.constData() + start, finish - start);
                return true;
            }
        }
    }

    return false;
}

void FirehoseManager::processBatch()
{
    bool anyEnabled = false;
    for (const auto &ep : this->endpoints_)
    {
        if (ep.enabledSetting && ep.enabledSetting->getValue())
        {
            anyEnabled = true;
            break;
        }
    }
    if (!anyEnabled)
    {
        std::lock_guard<std::mutex> lock(this->queueMutex_);
        this->rawQueue_.clear();
        return;
    }

    std::vector<QByteArray> batch;
    {
        std::lock_guard<std::mutex> lock(this->queueMutex_);
        if (this->rawQueue_.empty())
        {
            return;
        }
        batch.swap(this->rawQueue_);
    }

    std::vector<MessagePtr> parsedMessages;
    parsedMessages.reserve(batch.size());

    for (const auto &item : batch)
    {
        uint64_t rawHash = 0;
        bool hasHash = extractRawMessageHash(item, rawHash);
        if (hasHash)
        {
            if (this->seenHashes_.find(rawHash) != this->seenHashes_.end())
            {
                continue;  // duplicate message: dropped immediately without allocations
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
        auto msg = this->parseRawPayload(item, msgId);
        if (!msg)
        {
            continue;
        }

        if (!hasHash && !msgId.isEmpty())
        {
            QByteArray utf8 = msgId.toUtf8();
            uint64_t idHash = hashBytes64(utf8.constData(), utf8.size());
            if (this->seenHashes_.find(idHash) != this->seenHashes_.end())
            {
                continue;  // duplicate message
            }

            this->seenHashes_.insert(idHash);
            this->seenQueue_.push_back(idHash);

            if (this->seenQueue_.size() > MAX_DEDUP_CACHE_SIZE)
            {
                this->seenHashes_.erase(this->seenQueue_.front());
                this->seenQueue_.pop_front();
            }
        }

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
                        (msg->loginName.compare(target, Qt::CaseInsensitive) ==
                             0 ||
                         msg->displayName.compare(target,
                                                  Qt::CaseInsensitive) == 0))
                    {
                        stalk->addMessage(msg, MessageContext::Original);
                    }
                    ++it;
                }
                else
                {
                    it = this->stalkChannels_.erase(it);
                }
            }
        }

        parsedMessages.emplace_back(std::move(msg));
        this->messagesThisSecond_++;
    }

    if (!parsedMessages.empty())
    {
        this->channel_->addMessagesBatch(parsedMessages);
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
                return;
            }
            ++it;
        }
        else
        {
            it = this->stalkChannels_.erase(it);
        }
    }
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

    if (outMsgId.isEmpty())
    {
        outMsgId = QStringLiteral("%1:%2:%3")
                       .arg(targetChan, privMsg->nick(), privMsg->content());
    }

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
    if (outMsgId.isEmpty())
    {
        outMsgId = QStringLiteral("%1:%2:%3").arg(channel, username, text);
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
