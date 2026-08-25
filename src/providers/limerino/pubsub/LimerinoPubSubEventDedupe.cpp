// SPDX-License-Identifier: MIT

#include "providers/limerino/pubsub/LimerinoPubSubEventDedupe.hpp"

#include "providers/limerino/pubsub/LimerinoPubSubController.hpp"

#include <QCryptographicHash>
#include <QJsonDocument>
#include <QJsonObject>

#include <algorithm>

namespace chatterino::limerino {

namespace {

QJsonObject raidObject(const QJsonObject &payload)
{
    QJsonObject raid = payload.value(QStringLiteral("raid")).toObject();
    if (raid.isEmpty())
    {
        raid = payload.value(QStringLiteral("data"))
                   .toObject()
                   .value(QStringLiteral("raid"))
                   .toObject();
    }
    return raid;
}

QJsonObject pollObject(const QJsonObject &payload)
{
    return payload.value(QStringLiteral("data"))
        .toObject()
        .value(QStringLiteral("poll"))
        .toObject();
}

QJsonObject predictionEventObject(const QJsonObject &payload)
{
    const QJsonObject data = payload.value(QStringLiteral("data")).toObject();
    QJsonObject ev = data.value(QStringLiteral("event")).toObject();
    if (ev.isEmpty())
    {
        ev = data.value(QStringLiteral("prediction"))
                 .toObject()
                 .value(QStringLiteral("event"))
                 .toObject();
    }
    return ev;
}

QString hashPayload(const QJsonObject &payload)
{
    const QByteArray bytes =
        QJsonDocument(payload).toJson(QJsonDocument::Compact);
    return QString::fromLatin1(
        QCryptographicHash::hash(bytes, QCryptographicHash::Sha1).toHex());
}

}  // namespace

QString pubSubEventIdentity(const PubSubEvent &event)
{
    const QString type = event.eventType;
    const QJsonObject &payload = event.payload;

    if (type == QLatin1String("raid_update_v2") ||
        type == QLatin1String("raid_go_v2"))
    {
        const QString id =
            raidObject(payload).value(QStringLiteral("id")).toString();
        if (!id.isEmpty())
        {
            return type + QLatin1Char('\n') + id;
        }
    }

    if (type.startsWith(QLatin1String("POLL_")))
    {
        const QJsonObject poll = pollObject(payload);
        const QString pollId =
            poll.value(QStringLiteral("poll_id")).toString();
        const QString status = poll.value(QStringLiteral("status")).toString();
        if (!pollId.isEmpty())
        {
            return type + QLatin1Char('\n') + pollId + QLatin1Char('\n') +
                   status;
        }
    }

    if (type == QLatin1String("event-created") ||
        type == QLatin1String("event-updated") ||
        type == QLatin1String("event-completed") ||
        type == QLatin1String("event-cancelled"))
    {
        const QJsonObject ev = predictionEventObject(payload);
        const QString id = ev.value(QStringLiteral("id")).toString();
        const QString status = ev.value(QStringLiteral("status")).toString();
        if (!id.isEmpty())
        {
            return type + QLatin1Char('\n') + id + QLatin1Char('\n') + status;
        }
    }

    // Documented fallback: types with no natural key.
    return type + QLatin1Char('\n') + hashPayload(payload);
}

std::chrono::milliseconds pubSubDedupeWindowMs(const QString &eventType)
{
    if (eventType == QLatin1String("raid_update_v2"))
    {
        return std::chrono::milliseconds(120000);
    }
    return std::chrono::milliseconds(30000);
}

bool PubSubEventDedupe::isDuplicate(const QString &identity,
                                    std::chrono::milliseconds window)
{
    if (identity.isEmpty() || window.count() <= 0)
    {
        return false;
    }

    const auto now = std::chrono::steady_clock::now();
    this->pruneExpired(now);

    const auto it = std::find_if(
        this->entries_.begin(), this->entries_.end(),
        [&](const Entry &e) { return e.identity == identity; });
    if (it != this->entries_.end())
    {
        this->entries_.erase(it);
        this->entries_.push_back(Entry{identity, now + window});
        return true;
    }

    while (this->entries_.size() >= kMaxEntries)
    {
        this->entries_.erase(this->entries_.begin());
    }
    this->entries_.push_back(Entry{identity, now + window});
    return false;
}

void PubSubEventDedupe::clear()
{
    this->entries_.clear();
}

std::size_t PubSubEventDedupe::size() const
{
    return this->entries_.size();
}

void PubSubEventDedupe::pruneExpired(std::chrono::steady_clock::time_point now)
{
    this->entries_.erase(
        std::remove_if(this->entries_.begin(), this->entries_.end(),
                       [&](const Entry &e) { return e.expires <= now; }),
        this->entries_.end());
}

PubSubEventDedupe &pubSubEventDedupe()
{
    static PubSubEventDedupe instance;
    return instance;
}

}  // namespace chatterino::limerino
