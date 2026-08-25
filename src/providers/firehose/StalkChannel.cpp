// SPDX-FileCopyrightText: 2026 Contributors to Chatterino <https://chatterino.com>
//
// SPDX-License-Identifier: MIT

#include "providers/firehose/StalkChannel.hpp"

#include "Application.hpp"
#include "messages/Message.hpp"
#include "providers/firehose/FirehoseManager.hpp"
#include "singletons/Paths.hpp"

#include <QDir>
#include <QFile>
#include <QJsonArray>
#include <QJsonDocument>
#include <QJsonObject>
#include <QSaveFile>

namespace chatterino {

namespace {

constexpr size_t MAX_CACHED_STALK_MESSAGES = 1000;

QString sanitizeFileName(QString name)
{
    name = name.trimmed().toLower();
    for (auto &ch : name)
    {
        if (!ch.isLetterOrNumber() && ch != '_' && ch != '-')
        {
            ch = '_';
        }
    }
    return name;
}

}  // namespace

StalkChannel::StalkChannel(const QString &targetUser)
    : Channel(QStringLiteral("/stalk/") +
                  (targetUser.startsWith('#')
                       ? targetUser.mid(1).trimmed().toLower()
                       : targetUser.trimmed().toLower()),
              Channel::Type::TwitchStalk)
    , targetUser_(targetUser.startsWith('#') ? targetUser.mid(1).trimmed()
                                             : targetUser.trimmed())
    , customDisplayName_(QStringLiteral("Stalk — #%1")
                             .arg(targetUser.startsWith('#')
                                      ? targetUser.mid(1).trimmed()
                                      : targetUser.trimmed()))
    , saveTimer_(std::make_unique<QTimer>())
{
    this->saveTimer_->setSingleShot(true);
    QObject::connect(this->saveTimer_.get(), &QTimer::timeout, [this] {
        this->saveCache();
    });

    this->loadCache();
}

StalkChannel::~StalkChannel()
{
    if (this->saveTimer_ && this->saveTimer_->isActive())
    {
        this->saveTimer_->stop();
        this->saveCache();
    }
}

const QString &StalkChannel::getDisplayName() const
{
    return this->customDisplayName_;
}

const QString &StalkChannel::getLocalizedName() const
{
    return this->customDisplayName_;
}

const QString &StalkChannel::targetUser() const
{
    return this->targetUser_;
}

bool StalkChannel::canReconnect() const
{
    return true;
}

void StalkChannel::reconnect()
{
    if (auto *app = tryGetApp())
    {
        if (auto *mgr = app->getFirehose())
        {
            mgr->reconnectAll();
        }
    }
}

void StalkChannel::addStalkMessage(const MessagePtr &msg,
                                   const QByteArray &rawPayload)
{
    if (!msg)
    {
        return;
    }

    this->addMessage(msg, MessageContext::Original);

    StalkCachedItem item;
    item.timeMs = msg->serverReceivedTime.toMSecsSinceEpoch();
    item.channel = msg->channelName;
    item.username = msg->loginName;
    item.displayName = msg->displayName;
    item.text = msg->messageText;
    item.raw = QString::fromUtf8(rawPayload);

    this->cachedItems_.push_back(std::move(item));

    if (this->cachedItems_.size() > MAX_CACHED_STALK_MESSAGES)
    {
        this->cachedItems_.erase(
            this->cachedItems_.begin(),
            this->cachedItems_.begin() +
                (this->cachedItems_.size() - MAX_CACHED_STALK_MESSAGES));
    }

    this->scheduleSaveCache();
}

QString StalkChannel::getCacheFilePath() const
{
    auto *app = tryGetApp();
    if (!app)
    {
        return {};
    }

    const auto baseDir =
        app->getPaths().cacheDirectory() + QStringLiteral("/stalk");
    QDir().mkpath(baseDir);

    return baseDir + QStringLiteral("/") + sanitizeFileName(this->targetUser_) +
           QStringLiteral(".json");
}

void StalkChannel::loadCache()
{
    const auto filePath = this->getCacheFilePath();
    if (filePath.isEmpty() || !QFile::exists(filePath))
    {
        return;
    }

    QFile file(filePath);
    if (!file.open(QIODevice::ReadOnly))
    {
        return;
    }

    QJsonParseError err{};
    auto doc = QJsonDocument::fromJson(file.readAll(), &err);
    if (err.error != QJsonParseError::NoError || !doc.isObject())
    {
        return;
    }

    auto *app = tryGetApp();
    if (!app)
    {
        return;
    }
    auto *firehose = app->getFirehose();
    if (!firehose)
    {
        return;
    }

    const auto root = doc.object();
    const auto arr = root.value(QStringLiteral("messages")).toArray();

    std::vector<MessagePtr> loadedMessages;
    loadedMessages.reserve(arr.size());
    this->cachedItems_.reserve(arr.size());

    for (const auto &val : arr)
    {
        if (!val.isObject())
        {
            continue;
        }

        const auto obj = val.toObject();
        StalkCachedItem item;
        item.timeMs = obj.value(QStringLiteral("time")).toVariant().toLongLong();
        item.channel = obj.value(QStringLiteral("channel")).toString();
        item.username = obj.value(QStringLiteral("username")).toString();
        item.displayName = obj.value(QStringLiteral("displayName")).toString();
        item.text = obj.value(QStringLiteral("text")).toString();
        item.raw = obj.value(QStringLiteral("raw")).toString();

        QString msgId;
        MessagePtr msg;

        if (!item.raw.isEmpty())
        {
            msg = firehose->parseRawPayload(item.raw.toUtf8(), msgId);
        }

        if (!msg)
        {
            QString rawIrc =
                QStringLiteral(
                    "@display-name=%1;tmi-sent-ts=%2 :%3!%3@%3.tmi.twitch.tv "
                    "PRIVMSG #%4 :%5\r\n")
                    .arg(item.displayName.isEmpty() ? item.username
                                                    : item.displayName,
                         QString::number(item.timeMs), item.username,
                         item.channel, item.text);
            msg = firehose->parseRawPayload(rawIrc.toUtf8(), msgId);
        }

        if (msg)
        {
            loadedMessages.push_back(msg);
            this->cachedItems_.push_back(std::move(item));
        }
    }

    if (!loadedMessages.empty())
    {
        this->addMessagesAtStart(loadedMessages);
    }
}

void StalkChannel::scheduleSaveCache()
{
    if (this->saveTimer_)
    {
        this->saveTimer_->start(1000);
    }
}

void StalkChannel::saveCache()
{
    const auto filePath = this->getCacheFilePath();
    if (filePath.isEmpty())
    {
        return;
    }

    QJsonObject root;
    root[QStringLiteral("version")] = 1;
    root[QStringLiteral("targetUser")] = this->targetUser_;

    QJsonArray arr;
    for (const auto &item : this->cachedItems_)
    {
        QJsonObject obj;
        obj[QStringLiteral("time")] = item.timeMs;
        obj[QStringLiteral("channel")] = item.channel;
        obj[QStringLiteral("username")] = item.username;
        obj[QStringLiteral("displayName")] = item.displayName;
        obj[QStringLiteral("text")] = item.text;
        obj[QStringLiteral("raw")] = item.raw;
        arr.append(obj);
    }
    root[QStringLiteral("messages")] = arr;

    QSaveFile saveFile(filePath);
    if (saveFile.open(QIODevice::WriteOnly))
    {
        saveFile.write(QJsonDocument(root).toJson(QJsonDocument::Compact));
        saveFile.commit();
    }
}

}  // namespace chatterino
