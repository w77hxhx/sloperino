// SPDX-FileCopyrightText: 2026 Contributors to Chatterino <https://chatterino.com>
//
// SPDX-License-Identifier: MIT

#include "providers/twitch/MentionsCache.hpp"

#include "Application.hpp"
#include "messages/Message.hpp"
#include "messages/MessageBuilder.hpp"
#include "messages/MessageElement.hpp"
#include "singletons/Paths.hpp"
#include "singletons/Settings.hpp"

#include <pajlada/signals/signalholder.hpp>
#include <QColor>
#include <QDir>
#include <QFile>
#include <QJsonArray>
#include <QJsonDocument>
#include <QJsonObject>
#include <QSaveFile>
#include <QTime>

namespace chatterino {

namespace {

constexpr size_t MAX_CACHED_MENTIONS_MESSAGES = 1000;

}  // namespace

MentionsCache::MentionsCache(ChannelPtr channel)
    : channel_(std::move(channel))
    , saveTimer_(std::make_unique<QTimer>())
{
    if (!this->channel_)
    {
        return;
    }

    this->saveTimer_->setSingleShot(true);
    QObject::connect(this->saveTimer_.get(), &QTimer::timeout, [this] {
        this->saveCache();
    });

    this->holder_.managedConnect(this->channel_->messageAppended,
                                 [this](const auto &message, auto) {
                                     this->appendMessage(message);
                                 });

    this->loadCache();
}

MentionsCache::~MentionsCache()
{
    if (this->saveTimer_ && this->saveTimer_->isActive())
    {
        this->saveTimer_->stop();
        this->saveCache();
    }
}

void MentionsCache::appendMessage(const MessagePtr &message)
{
    if (!message || message->flags.has(MessageFlag::System) ||
        message->id.isEmpty())
    {
        return;
    }

    CachedItem item;
    item.id = message->id;
    item.timeMs = message->serverReceivedTime.toMSecsSinceEpoch();
    item.channelName = message->channelName;
    item.loginName = message->loginName;
    item.displayName = message->displayName;
    item.text = message->messageText;
    item.color = message->usernameColor.isValid()
                     ? message->usernameColor.name(QColor::HexArgb)
                     : QString{};
    item.highlighted = message->flags.has(MessageFlag::Highlighted);

    // Skip duplicates (mentions channel dedupes by id as well).
    if (this->cachedIds_.contains(item.id))
    {
        return;
    }
    this->cachedIds_.insert(item.id);

    this->cachedItems_.push_back(std::move(item));

    if (this->cachedItems_.size() > MAX_CACHED_MENTIONS_MESSAGES)
    {
        const auto removeCount =
            this->cachedItems_.size() - MAX_CACHED_MENTIONS_MESSAGES;
        for (size_t i = 0; i < removeCount; ++i)
        {
            this->cachedIds_.remove(this->cachedItems_[i].id);
        }
        this->cachedItems_.erase(
            this->cachedItems_.begin(),
            this->cachedItems_.begin() + static_cast<qsizetype>(removeCount));
    }

    this->scheduleSaveCache();
}

QString MentionsCache::getCacheFilePath() const
{
    auto *app = tryGetApp();
    if (!app)
    {
        return {};
    }

    const auto baseDir =
        app->getPaths().cacheDirectory() + QStringLiteral("/mentions");
    QDir().mkpath(baseDir);

    return baseDir + QStringLiteral("/mentions.json");
}

void MentionsCache::loadCache()
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

    const auto root = doc.object();
    const auto arr = root.value(QStringLiteral("messages")).toArray();

    std::vector<MessagePtr> loadedMessages;
    loadedMessages.reserve(arr.size());

    for (const auto &val : arr)
    {
        if (!val.isObject())
        {
            continue;
        }

        const auto obj = val.toObject();
        CachedItem item;
        item.id = obj.value(QStringLiteral("id")).toString();
        item.timeMs =
            obj.value(QStringLiteral("time")).toVariant().toLongLong();
        item.channelName = obj.value(QStringLiteral("channel")).toString();
        item.loginName = obj.value(QStringLiteral("login")).toString();
        item.displayName = obj.value(QStringLiteral("display")).toString();
        item.text = obj.value(QStringLiteral("text")).toString();
        item.color = obj.value(QStringLiteral("color")).toString();
        item.highlighted =
            obj.value(QStringLiteral("highlighted")).toBool(false);

        if (item.id.isEmpty())
        {
            continue;
        }

        MessageBuilder builder;

        builder.message().id = item.id;
        builder.message().channelName = item.channelName;
        builder.message().loginName = item.loginName;
        builder.message().displayName = item.displayName;
        builder.message().messageText = item.text;
        builder.message().serverReceivedTime =
            QDateTime::fromMSecsSinceEpoch(item.timeMs);
        builder.message().flags.set(MessageFlag::ShowInMentions);
        if (item.highlighted)
        {
            builder.message().flags.set(MessageFlag::Highlighted);
        }
        if (!item.color.isEmpty())
        {
            builder.message().usernameColor = QColor(item.color);
        }

        const auto when = QDateTime::fromMSecsSinceEpoch(item.timeMs).time();

        builder.emplace<TimestampElement>(when);
        const auto usernameColor = item.color.isEmpty()
                                       ? MessageColor(MessageColor::System)
                                       : MessageColor(QColor(item.color));
        builder.emplace<TextElement>(item.displayName,
                                     MessageElementFlag::Username,
                                     usernameColor, FontStyle::ChatMediumBold);
        builder.emplace<TextElement>(":", MessageElementFlag::Username,
                                     MessageColor::Text);
        builder.emplace<TextElement>(item.text, MessageElementFlag::Text,
                                     MessageColor::Text);

        loadedMessages.push_back(builder.release());
        this->cachedItems_.push_back(std::move(item));
        this->cachedIds_.insert(this->cachedItems_.back().id);
    }

    if (!loadedMessages.empty())
    {
        // Inserts in serverReceivedTime order and avoids re-saving below.
        this->channel_->fillInMissingMessages(loadedMessages);
    }
}

void MentionsCache::scheduleSaveCache()
{
    if (this->saveTimer_)
    {
        this->saveTimer_->start(1000);
    }
}

void MentionsCache::saveCache()
{
    const auto filePath = this->getCacheFilePath();
    if (filePath.isEmpty())
    {
        return;
    }

    QJsonObject root;
    root[QStringLiteral("version")] = 1;

    QJsonArray arr;
    for (const auto &item : this->cachedItems_)
    {
        QJsonObject obj;
        obj[QStringLiteral("id")] = item.id;
        obj[QStringLiteral("time")] = static_cast<qint64>(item.timeMs);
        obj[QStringLiteral("channel")] = item.channelName;
        obj[QStringLiteral("login")] = item.loginName;
        obj[QStringLiteral("display")] = item.displayName;
        obj[QStringLiteral("text")] = item.text;
        obj[QStringLiteral("color")] = item.color;
        obj[QStringLiteral("highlighted")] = item.highlighted;
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
