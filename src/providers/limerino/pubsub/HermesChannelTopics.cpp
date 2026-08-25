// SPDX-License-Identifier: MIT

#include "providers/limerino/pubsub/HermesChannelTopics.hpp"

#include "providers/limerino/pubsub/LimerinoPubSubController.hpp"
#include "providers/limerino/pubsub/LimerinoPubSubTopics.hpp"
#include "providers/twitch/TwitchChannel.hpp"

#include <QJsonArray>
#include <QJsonObject>
#include <QString>
#include <QVariant>

namespace chatterino::limerino {

namespace {

// --- raid.<channelID> -------------------------------------------------------
//
// Reference: events.js L78-129 (raid_update_v2). raid_go_v2 shares the same
// raid object shape; it means the raid has started (distinct from pending).

QString formatRaidDisplay(const QJsonObject &raid, bool started)
{
    const QString fromId = raid[QStringLiteral("source_id")].toString();
    const QString toL = raid[QStringLiteral("target_login")].toString();
    const QString toD = raid[QStringLiteral("target_display_name")].toString();
    const QString to = toD.isEmpty() ? toL : toD;

    // source_id is numeric on the wire; displayChannelId (set by handleRaid)
    // resolves it via Helix before emit. Until then / on failure the
    // controller prefixes unresolved ids with "id:".
    const QString verb = started ? QStringLiteral("raid started from %1 to %2")
                                 : QStringLiteral("raid from %1 to %2");
    QString text = verb.arg(fromId, to);

    const QString creatorId = raid[QStringLiteral("creator_id")].toString();
    if (!creatorId.isEmpty() && creatorId != fromId)
    {
        text += QStringLiteral(" (created by id:%1)").arg(creatorId);
    }

    const int viewers = raid[QStringLiteral("viewer_count")].toInt(-1);
    if (viewers >= 0)
    {
        text += QStringLiteral(" · %1 viewers").arg(viewers);
    }
    return text;
}

// --- polls.<channelID> / predictions-channel-v1.<channelID> ----------------

QString sharedPredictionEventText(const QJsonObject &data, const QString &type,
                                  bool forUserTopic)
{
    const QJsonObject event = data[QStringLiteral("event")].toObject();
    const QString title = event[QStringLiteral("title")].toString();
    if (title.isEmpty())
    {
        return {};
    }
    const QJsonArray outcomes = event[QStringLiteral("outcomes")].toArray();
    QString opts;
    for (int i = 0; i < outcomes.size(); ++i)
    {
        if (!opts.isEmpty())
        {
            opts += QStringLiteral(" / ");
        }
        opts += outcomes.at(i).toObject()[QStringLiteral("title")].toString();
    }
    const QString who =
        forUserTopic ? QStringLiteral("your ") : QString();
    if (type == QLatin1String("event-created"))
    {
        return opts.isEmpty()
                   ? QStringLiteral("%1prediction started: %2").arg(who, title)
                   : QStringLiteral("%1prediction started: %2 [%3]")
                         .arg(who, title, opts);
    }
    if (type == QLatin1String("event-updated"))
    {
        const QString status = event[QStringLiteral("status")].toString();
        QString suffix;
        if (status == QLatin1String("ACTIVE"))
        {
            suffix = QStringLiteral("active");
        }
        else if (status == QLatin1String("LOCKED"))
        {
            suffix = QStringLiteral("locked");
        }
        else if (status == QLatin1String("RESOLVED"))
        {
            suffix = QStringLiteral("resolved");
        }
        else if (status == QLatin1String("CANCEL_PENDING") ||
                 status == QLatin1String("CANCELED"))
        {
            suffix = QStringLiteral("cancelled");
        }
        else
        {
            suffix = status;
        }
        return suffix.isEmpty()
                   ? QStringLiteral("%1prediction updated: %2").arg(who, title)
                   : QStringLiteral("%1prediction updated: %2 (%3)")
                         .arg(who, title, suffix);
    }
    return {};
}

/// POLL_CREATE: title + numbered choices, no zero vote noise.
/// POLL_UPDATE / POLL_COMPLETE / POLL_END / POLL_ARCHIVE: include totals.
QString describePoll(const QJsonObject &payload)
{
    const QString type = payload[QStringLiteral("type")].toString();
    const QJsonObject poll = payload[QStringLiteral("data")]
                                 .toObject()[QStringLiteral("poll")]
                                 .toObject();
    const QString title = poll[QStringLiteral("title")].toString();
    const QJsonArray choices = poll[QStringLiteral("choices")].toArray();

    // CREATE: all counts are zero — listing "(0)" on every choice is noise.
    // UPDATE / END / COMPLETE / ARCHIVE: include live or final totals.
    const bool includeCounts = type != QLatin1String("POLL_CREATE");

    QString choicesText;
    for (int i = 0; i < choices.size(); ++i)
    {
        const QJsonObject choice = choices.at(i).toObject();
        const QString cTitle = choice[QStringLiteral("title")].toString();
        if (cTitle.isEmpty())
        {
            continue;
        }
        if (!choicesText.isEmpty())
        {
            choicesText += QStringLiteral(" · ");
        }
        choicesText += QStringLiteral("%1. %2").arg(i + 1).arg(cTitle);
        if (includeCounts)
        {
            const int votes =
                choice[QStringLiteral("votes")].toObject()
                    [QStringLiteral("total")]
                        .toInt();
            choicesText += QStringLiteral(" (%1)").arg(votes);
        }
    }

    QString head = title.isEmpty() ? type : title;
    if (type == QLatin1String("POLL_UPDATE"))
    {
        head = QStringLiteral("poll update: %1").arg(head);
    }
    else if (type == QLatin1String("POLL_COMPLETE") ||
             type == QLatin1String("POLL_END") ||
             type == QLatin1String("POLL_ARCHIVE"))
    {
        head = QStringLiteral("poll ended: %1").arg(head);
    }
    else if (type == QLatin1String("POLL_CREATE"))
    {
        head = QStringLiteral("poll: %1").arg(head);
    }

    if (poll[QStringLiteral("settings")]
            .toObject()[QStringLiteral("multi_choice")]
            .toObject()[QStringLiteral("is_enabled")]
            .toBool())
    {
        head += QStringLiteral(" (multi)");
    }

    if (choicesText.isEmpty())
    {
        return head;
    }
    return head + QStringLiteral(" — ") + choicesText;
}

bool handleRaid(const QString & /*topic*/, const QJsonObject &payload,
                PubSubEvent &event)
{
    const QString type = payload[QStringLiteral("type")].toString();
    const bool pending = type == QLatin1String("raid_update_v2");
    const bool started = type == QLatin1String("raid_go_v2");
    if (!pending && !started)
    {
        return false;
    }

    QJsonObject raid = payload[QStringLiteral("raid")].toObject();
    if (raid.isEmpty())
    {
        raid = payload[QStringLiteral("data")].toObject()[QStringLiteral("raid")]
                   .toObject();
    }
    if (raid.isEmpty())
    {
        return false;
    }

    const QString sourceId = raid[QStringLiteral("source_id")].toString();
    event.displayText = formatRaidDisplay(raid, started);
    if (!sourceId.isEmpty())
    {
        // Wire into existing Helix batch resolver (E1.b / B4.2).
        event.displayChannelId = sourceId;
    }
    return true;
}

bool handlePredictionsChannel(const QString & /*topic*/,
                              const QJsonObject &payload, PubSubEvent &event)
{
    const QString type = payload[QStringLiteral("type")].toString();
    if (type != QLatin1String("event-created") &&
        type != QLatin1String("event-updated"))
    {
        return false;
    }
    event.displayText = sharedPredictionEventText(
        payload[QStringLiteral("data")].toObject(), type, false);
    return !event.displayText.isEmpty();
}

bool handlePollChannel(const QString & /*topic*/, const QJsonObject &payload,
                       PubSubEvent &event)
{
    event.displayText = describePoll(payload);
    return true;
}

}  // namespace

void installHermesChannelTopicHandlers(LimerinoPubSubController &controller)
{
    controller.registerTopicHandler(QStringLiteral("raid."), &handleRaid);
    controller.registerTopicHandler(QStringLiteral("predictions-channel-v1."),
                                    &handlePredictionsChannel);
    controller.registerTopicHandler(QStringLiteral("polls."),
                                    &handlePollChannel);

    controller.registerKnownEventType(QStringLiteral("raid_update_v2"));
    controller.registerKnownEventType(QStringLiteral("raid_go_v2"));
    controller.registerKnownEventType(QStringLiteral("event-created"));
    controller.registerKnownEventType(QStringLiteral("event-updated"));
    controller.registerKnownEventType(QStringLiteral("POLL_CREATE"));
    controller.registerKnownEventType(QStringLiteral("POLL_UPDATE"));
    controller.registerKnownEventType(QStringLiteral("POLL_COMPLETE"));
    controller.registerKnownEventType(QStringLiteral("POLL_END"));
    controller.registerKnownEventType(QStringLiteral("POLL_ARCHIVE"));
}

void ensureHermesChannelTopics(const TwitchChannel &channel)
{
    const QString id = channel.roomId();
    if (id.isEmpty())
    {
        return;
    }
    auto *c = getPubSubController();
    c->ensureTopic(QStringLiteral("raid.%1").arg(id), PubSubTopicAuth::None);
    c->ensureTopic(QStringLiteral("polls.%1").arg(id), PubSubTopicAuth::None);
    c->ensureTopic(QStringLiteral("predictions-channel-v1.%1").arg(id),
                   PubSubTopicAuth::None);
}

}  // namespace chatterino::limerino
