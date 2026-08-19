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
#include "util/PostToThread.hpp"

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
        this->mgr_->onRawDataReceivedFromWorker(data.constData(), data.size());
    }

    void onBinaryMessage(QByteArray data) override
    {
        this->mgr_->onRawDataReceivedFromWorker(data.constData(), data.size());
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

    // 16ms batch timer (~60fps smooth UI delivery)
    this->batchTimer_.setTimerType(Qt::PreciseTimer);
    this->batchTimer_.setInterval(
        getSettings()->firehoseBatchIntervalMs.getValue());
    QObject::connect(&this->batchTimer_, &QTimer::timeout, this,
                     &FirehoseManager::processBatch);

    getSettings()->firehoseBatchIntervalMs.connect(
        [this](int ms) {
            this->batchTimer_.setInterval(std::clamp(ms, 5, 1000));
        },
        this->signalHolder_);

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
    this->batchTimer_.stop();
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
    return this->firehoseAttachedCount_.load(std::memory_order_relaxed) > 0 ||
           !this->stalkChannels_.empty();
}

void FirehoseManager::checkConnectionState()
{
    if (this->isNeeded())
    {
        if (!this->isRunning_)
        {
            this->isRunning_ = true;
            this->secondsSinceLastSpeedCheck_ = 0;
            this->initialize();
            this->batchTimer_.start();
            this->statsTimer_.start();
            this->watchdogTimer_.start();
        }
    }
    else
    {
        if (this->isRunning_)
        {
            this->isRunning_ = false;
            this->secondsSinceLastSpeedCheck_ = 0;
            this->batchTimer_.stop();
            this->statsTimer_.stop();
            this->watchdogTimer_.stop();
            for (size_t i = 0; i < this->endpoints_.size(); ++i)
            {
                this->disconnectEndpoint(i);
            }
            std::lock_guard<std::mutex> lock(this->queueMutex_);
            this->incomingQueue_.clear();
            this->dedupCache_.clear();
        }
    }
}

void FirehoseManager::addFirehoseConsumer()
{
    this->firehoseAttachedCount_.fetch_add(1, std::memory_order_relaxed);
    this->checkConnectionState();
}

void FirehoseManager::removeFirehoseConsumer()
{
    int current = this->firehoseAttachedCount_.load(std::memory_order_relaxed);
    while (current > 0)
    {
        if (this->firehoseAttachedCount_.compare_exchange_weak(
                current, current - 1, std::memory_order_relaxed))
        {
            break;
        }
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

    this->secondsSinceLastSpeedCheck_ = 0;

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

    this->secondsSinceLastSpeedCheck_ = 0;

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

static bool extractRawMessageHashFast(const char *ptr, size_t size,
                                      uint64_t &outHash) noexcept
{
    if (size < 4)
    {
        return false;
    }

    size_t i = 0;
    while (i < size && (ptr[i] == ' ' || ptr[i] == '\t' || ptr[i] == '\r' ||
                        ptr[i] == '\n'))
    {
        i++;
    }

    if (i >= size)
    {
        return false;
    }

    std::string_view sv(ptr + i, size - i);

    if (sv.front() == '{')
    {
        size_t idPos = sv.find("\"id\":");
        if (idPos == std::string_view::npos)
        {
            idPos = sv.find("\"id\" :");
        }
        if (idPos != std::string_view::npos)
        {
            size_t quoteStart = sv.find('"', idPos + 4);
            if (quoteStart != std::string_view::npos)
            {
                size_t quoteEnd = sv.find('"', quoteStart + 1);
                if (quoteEnd != std::string_view::npos &&
                    quoteEnd > quoteStart + 1)
                {
                    outHash = hashBytes64(sv.data() + quoteStart + 1,
                                          quoteEnd - quoteStart - 1);
                    return true;
                }
            }
        }
        return false;
    }

    if (sv.front() == '@')
    {
        size_t spacePos = sv.find(' ');
        if (spacePos == std::string_view::npos)
        {
            return false;
        }

        std::string_view tags = sv.substr(0, spacePos);
        size_t idStart = std::string_view::npos;

        if (tags.starts_with("@id="))
        {
            idStart = 4;
        }
        else
        {
            size_t found = tags.find(";id=");
            if (found != std::string_view::npos)
            {
                idStart = found + 4;
            }
        }

        if (idStart != std::string_view::npos)
        {
            size_t semi = tags.find(';', idStart);
            size_t idLen = (semi == std::string_view::npos)
                               ? (tags.size() - idStart)
                               : (semi - idStart);
            if (idLen > 0)
            {
                outHash = hashBytes64(tags.data() + idStart, idLen);
                return true;
            }
        }
    }

    return false;
}

void FirehoseManager::onRawDataReceivedFromWorker(const char *ptr, size_t len)
{
    if (len == 0 || !this->isNeeded())
    {
        return;
    }

    // Count raw incoming messages across all endpoints for atomic rate reporting
    int rawCount = 0;
    size_t start = 0;
    std::vector<QByteArray> newItems;

    while (start < len)
    {
        const char *nextPtr = static_cast<const char *>(
            std::memchr(ptr + start, '\n', len - start));
        size_t next = nextPtr ? (nextPtr - ptr) : len;
        size_t lineLen = next - start;

        if (lineLen > 0 && ptr[start + lineLen - 1] == '\r')
        {
            lineLen--;
        }

        if (lineLen > 0)
        {
            rawCount++;

            uint64_t hash = 0;
            if (extractRawMessageHashFast(ptr + start, lineLen, hash))
            {
                if (!this->dedupCache_.testAndSet(hash))
                {
                    start = next + 1;
                    continue;  // Duplicate across firehose endpoints: dropped in worker thread
                }
            }

            // If firehose is not open and we only have stalk channels, quick check
            if (this->firehoseAttachedCount_.load(std::memory_order_relaxed) ==
                0)
            {
                bool mightMatchStalk = false;
                std::string_view lineSv(ptr + start, lineLen);
                {
                    std::lock_guard<std::mutex> lock(this->stalkMutex_);
                    for (const auto &weakStalk : this->stalkChannels_)
                    {
                        if (auto stalk = weakStalk.lock())
                        {
                            const auto &target = stalk->targetUser();
                            if (!target.isEmpty() &&
                                lineSv.find(target.toUtf8().constData()) !=
                                    std::string_view::npos)
                            {
                                mightMatchStalk = true;
                                break;
                            }
                        }
                    }
                }
                if (!mightMatchStalk)
                {
                    start = next + 1;
                    continue;
                }
            }

            newItems.emplace_back(ptr + start, static_cast<int>(lineLen));
        }

        start = next + 1;
    }

    if (rawCount > 0)
    {
        this->rawMessagesReceived_.fetch_add(rawCount,
                                             std::memory_order_relaxed);
    }

    if (!newItems.empty())
    {
        std::lock_guard<std::mutex> lock(this->queueMutex_);
        if (this->incomingQueue_.size() < 100000)
        {
            this->incomingQueue_.insert(
                this->incomingQueue_.end(),
                std::make_move_iterator(newItems.begin()),
                std::make_move_iterator(newItems.end()));
        }
    }
}

void FirehoseManager::processBatch()
{
    if (!this->isNeeded())
    {
        return;
    }

    std::vector<QByteArray> batch;
    {
        std::lock_guard<std::mutex> lock(this->queueMutex_);
        if (this->incomingQueue_.empty())
        {
            return;
        }
        batch.swap(this->incomingQueue_);
    }

    std::vector<MessagePtr> parsedMessages;
    parsedMessages.reserve(std::min(batch.size(), size_t(300)));

    const bool firehoseOpen =
        (this->firehoseAttachedCount_.load(std::memory_order_relaxed) > 0);

    for (const auto &item : batch)
    {
        QString msgId;
        auto msg = this->parseRawPayload(item, msgId);
        if (!msg)
        {
            continue;
        }

        // Route message to active stalk channels
        {
            std::lock_guard<std::mutex> lock(this->stalkMutex_);
            if (!this->stalkChannels_.empty())
            {
                for (auto it = this->stalkChannels_.begin();
                     it != this->stalkChannels_.end();)
                {
                    if (auto stalk = it->lock())
                    {
                        const auto &target = stalk->targetUser();
                        if (!target.isEmpty() &&
                            (msg->loginName.compare(target,
                                                    Qt::CaseInsensitive) == 0 ||
                             msg->displayName.compare(
                                 target, Qt::CaseInsensitive) == 0))
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
        }

        // Route message to global /mentions channel
        if (msg->flags.has(MessageFlag::Highlighted) &&
            msg->flags.has(MessageFlag::ShowInMentions))
        {
            if (auto mentions = getApp()->getTwitch()->getMentionsChannel())
            {
                mentions->addMessage(msg, MessageContext::Original);
            }
        }

        if (firehoseOpen)
        {
            parsedMessages.emplace_back(std::move(msg));
        }
    }

    if (!parsedMessages.empty() && this->channel_)
    {
        this->channel_->addMessagesBatch(parsedMessages);
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

    {
        std::lock_guard<std::mutex> lock(this->stalkMutex_);
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
    this->checkConnectionState();
}

void FirehoseManager::unregisterStalkChannel(
    const std::shared_ptr<StalkChannel> &channel)
{
    if (!channel)
    {
        return;
    }

    {
        std::lock_guard<std::mutex> lock(this->stalkMutex_);
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
    }
    this->checkConnectionState();
}

void FirehoseManager::updateStats()
{
    this->currentMsgPerSecond_ =
        this->rawMessagesReceived_.exchange(0, std::memory_order_relaxed);

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

    if (this->isNeeded() && this->isRunning_)
    {
        this->secondsSinceLastSpeedCheck_++;
        if (this->secondsSinceLastSpeedCheck_ >= 60)
        {
            this->secondsSinceLastSpeedCheck_ = 0;
            if (this->currentMsgPerSecond_ < 1000)
            {
                qCDebug(chatterinoWebsocket)
                    << "Firehose speed is below 1000 msg/s ("
                    << this->currentMsgPerSecond_
                    << " msg/s). Reconnecting to firehose...";
                this->reconnectAll();
            }
        }
    }
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
