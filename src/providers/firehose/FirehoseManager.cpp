// SPDX-FileCopyrightText: 2026 Contributors to Chatterino <https://chatterino.com>
//
// SPDX-License-Identifier: MIT

#include "providers/firehose/FirehoseManager.hpp"

#include "Application.hpp"
#include "common/QLogging.hpp"
#include "messages/Message.hpp"
#include "messages/MessageBuilder.hpp"
#include "providers/twitch/IrcMessageHandler.hpp"
#include "providers/twitch/TwitchIrcServer.hpp"
#include "singletons/Settings.hpp"
#include "util/Helpers.hpp"

#include <IrcCommand>
#include <IrcMessage>
#include <QJsonArray>
#include <QJsonDocument>
#include <QJsonObject>
#include <QRegularExpression>

#include <chrono>

namespace chatterino {

namespace {

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
                // Endpoint connected
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

}  // namespace

FirehoseManager::FirehoseManager()
    : channel_(std::make_shared<FirehoseChannel>())
{
    this->initEndpoints();

    // 250ms batch processing timer
    this->batchTimer_.setInterval(
        getSettings()->firehoseBatchIntervalMs.getValue());
    QObject::connect(&this->batchTimer_, &QTimer::timeout, this,
                     &FirehoseManager::processBatch);
    this->batchTimer_.start();

    getSettings()->firehoseBatchIntervalMs.connect(
        [this](int ms) {
            this->batchTimer_.setInterval(std::clamp(ms, 50, 2000));
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

    this->endpoints_ = {
        Endpoint{
            .name = QStringLiteral("Spanix"),
            .url = QUrl(QStringLiteral("wss://logs.spanix.team/firehose")),
            .enabledSetting = &s.firehoseEnableSpanix,
        },
        Endpoint{
            .name = QStringLiteral("Supa"),
            .url = QUrl(QStringLiteral("wss://logs.supa.codes/firehose")),
            .enabledSetting = &s.firehoseEnableSupa,
        },
        Endpoint{
            .name = QStringLiteral("Susgee"),
            .url = QUrl(QStringLiteral("wss://logs.susgee.dev/firehose")),
            .enabledSetting = &s.firehoseEnableSusgee,
        },
        Endpoint{
            .name = QStringLiteral("Nadeko"),
            .url = QUrl(QStringLiteral("wss://logs.nadeko.net/firehose")),
            .enabledSetting = &s.firehoseEnableNadeko,
        },
        Endpoint{
            .name = QStringLiteral("Logxx"),
            .url = QUrl(QStringLiteral("wss://logxx.dev/firehose")),
            .enabledSetting = &s.firehoseEnableLogxx,
        },
        Endpoint{
            .name = QStringLiteral("Catquery"),
            .url = QUrl(QStringLiteral("wss://firehose.catquery.com")),
            .enabledSetting = &s.firehoseEnableCatquery,
        },
    };

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
        if (!this->endpoints_[i].enabledSetting ||
            this->endpoints_[i].enabledSetting->getValue())
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
        if (!this->endpoints_[i].enabledSetting ||
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
    ep.isConnected = true;
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
    this->rawQueue_.emplace_back(std::move(data));
}

void FirehoseManager::processBatch()
{
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
        QString msgId;
        auto msg = this->parseRawPayload(item, msgId);
        if (!msg)
        {
            continue;
        }

        // Deduplication
        std::string idStr = msgId.toStdString();
        if (!idStr.empty())
        {
            if (this->seenIds_.find(idStr) != this->seenIds_.end())
            {
                continue;  // duplicate message
            }

            this->seenIds_.insert(idStr);
            this->seenQueue_.push_back(idStr);

            if (this->seenQueue_.size() > MAX_DEDUP_CACHE_SIZE)
            {
                this->seenIds_.erase(this->seenQueue_.front());
                this->seenQueue_.pop_front();
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

    if (ircMsg->command() != u"PRIVMSG"_s)
    {
        ircMsg->deleteLater();
        return nullptr;
    }

    auto *privMsg = dynamic_cast<Communi::IrcPrivateMessage *>(ircMsg);
    if (!privMsg)
    {
        ircMsg->deleteLater();
        return nullptr;
    }

    QString targetChan;
    trimChannelName(privMsg->target(), targetChan);

    if (outMsgId.isEmpty())
    {
        outMsgId = QStringLiteral("%1:%2:%3")
                       .arg(targetChan, privMsg->nick(), privMsg->content());
    }

    Channel chan(targetChan, Channel::Type::Twitch);

    MessageParseArgs args;
    args.isAction = privMsg->isAction();

    QString content = unescapeZeroWidthJoiner(privMsg->content());
    int messageOffset = stripLeadingReplyMention(tags, content);

    auto [builtMsg, alert] = MessageBuilder::makeIrcMessage(
        &chan, privMsg, args, content, messageOffset);

    ircMsg->deleteLater();

    if (!builtMsg)
    {
        return nullptr;
    }

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
