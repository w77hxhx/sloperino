// SPDX-License-Identifier: MIT

#include "providers/limerino/pubsub/HermesUserTopics.hpp"

#include "Application.hpp"
#include "common/Channel.hpp"
#include "common/QLogging.hpp"
#include "messages/MessageBuilder.hpp"
#include "messages/MessageElement.hpp"
#include "messages/Link.hpp"
#include "messages/MessageColor.hpp"
#include "providers/limerino/LimerinoAuth.hpp"
#include "providers/limerino/commands/Identity.hpp"
#include "providers/limerino/gql/LimerinoGql.hpp"
#include "providers/limerino/gql/PersistedQueries.hpp"
#include "providers/limerino/pubsub/LimerinoChannelNameResolver.hpp"
#include "providers/limerino/pubsub/LimerinoPubSubController.hpp"
#include "providers/limerino/pubsub/LimerinoPubSubTopics.hpp"
#include "providers/twitch/TwitchChannel.hpp"
#include "providers/twitch/TwitchIrcServer.hpp"
#include "singletons/Settings.hpp"

#include <QDateTime>
#include <QJsonArray>
#include <QJsonObject>
#include <QSet>
#include <QString>

#include <array>
#include <unordered_map>

namespace chatterino::limerino {

namespace {

// chatrooms-user-v1 reference cooldown (events.js L14). Kept channel-local,
// so a warn burst in one channel doesn't suppress the next channel's.
constexpr int USER_MOD_ACTION_COOLDOWN_MS = 2500;

struct WarnCooldown {
    qint64 issuedAtMs = 0;
};
std::unordered_map<QString, WarnCooldown> &warnCooldowns()
{
    static auto *map = new std::unordered_map<QString, WarnCooldown>;
    return *map;
}

bool warnRecentlyIn(const QString &channelId)
{
    auto &map = warnCooldowns();
    const auto now = QDateTime::currentMSecsSinceEpoch();
    auto it = map.find(channelId);
    if (it != map.end() && now - it->second.issuedAtMs < USER_MOD_ACTION_COOLDOWN_MS)
    {
        return true;
    }
    map[channelId] = WarnCooldown{now};
    return false;
}

// Resolve a display name best-effort for chat reads. Sync hits only (open
// channel / auth lists / session cache); unresolved ids stay as the numeric
// id and LimerinoPubSubController replaces them after Helix resolve (E1.b).
QString describeChannel(const QString &channelId)
{
    QString name;
    if (trySyncChannelName(channelId, name))
    {
        return name;
    }
    return channelId;
}

void acknowledgeWarningFor(const QString &channelId)
{
    if (!getSettings()->limerinoAutoAcknowledgeChatWarnings.getValue())
    {
        return;
    }

    // Same mutation as /acknowledgewarning; from PersistedQueries.
    const QString gqlToken = [&]() {
        QString err;
        auto t = LimerinoAuth::resolveCurrentUserToken(&err);
        return t.hasToken() ? t.token : QString();
    }();
    if (gqlToken.isEmpty())
    {
        return;  // silently skip: the warning line already says why
    }

    LimerinoAuth::gql::executePersisted(
        LimerinoAuth::gql::PQ_ACKNOWLEDGE_CHAT_WARNING,
        QJsonObject{{QStringLiteral("input"),
                     QJsonObject{{QStringLiteral("channelID"), channelId}}}},
        gqlToken, [](const QJsonObject & /*data*/) {},
        [](const auto & /*error*/) {});
}

// Actions that get side effects (channel surface + auto-acknowledge). Any
// other action still renders as a generic events line.
constexpr std::array<const char *, 2> USER_MOD_ACTION_ALLOWED = {
    "warn",
    "acknowledge_warning",
};

bool isAllowedUserModAction(QStringView action)
{
    for (const char *allowed : USER_MOD_ACTION_ALLOWED)
    {
        if (action == QLatin1String(allowed))
        {
            return true;
        }
    }
    return false;
}

bool handleUserModerationAction(const QJsonObject &data,
                                PubSubEvent &event)
{
    const QString action = data[QStringLiteral("action")].toString();
    const QString channelId = data[QStringLiteral("channel_id")].toString();
    const QString reason = data[QStringLiteral("reason")].toString();
    // event.channelId is the USER id for this user-scoped topic (the topic
    // suffix), NOT the channel id - keep the two straight.
    if (action.isEmpty() || channelId.isEmpty())
    {
        return false;
    }

    event.displayChannelId = channelId;

    // Plain-language line; unknown action types still render honestly.
    QString text;
    if (action == QLatin1String("warn"))
    {
        text = QStringLiteral("You were warned in %1")
                   .arg(describeChannel(channelId));
    }
    else if (action == QLatin1String("acknowledge_warning"))
    {
        text = QStringLiteral("You acknowledged the warning in %1")
                   .arg(describeChannel(channelId));
    }
    else
    {
        text = QStringLiteral("Moderation action \"%1\" in %2")
                   .arg(action, describeChannel(channelId));
    }
    if (!reason.isEmpty())
    {
        text += QStringLiteral(" - reason: %1").arg(reason);
    }
    event.displayText = text;

    if (!isAllowedUserModAction(action))
    {
        return true;  // rendered generically; no side effects for other actions
    }

    // events.js L27: only act when the action targets our own user id
    // (the topic is user-scoped already, but transcribe the filter anyway).
    const QString targetId = data[QStringLiteral("target_id")].toString();
    if (!targetId.isEmpty() && targetId != event.channelId)
    {
        return false;
    }

    if (action == QLatin1String("warn"))
    {
        // Surface in the channel (decision P2-Q2), then auto-acknowledge if
        // the user enabled it (decision P2-Q1; reference events.js L60).
        auto channelPtr = getApp()->getTwitch()->getChannelOrEmptyByID(channelId);
        if (!channelPtr->isEmpty())
        {
            auto *tchan = dynamic_cast<TwitchChannel *>(channelPtr.get());
            if (tchan != nullptr)
            {
                MessageBuilder b;
                // TwitchChannel::addSystemMessage is the simple text piston;
                // here we need a trailing Acknowledge link button.
                b.emplace<TextElement>(text, MessageElementFlag::Text,
                                       MessageColor::System);
                b.emplace<TextElement>(QStringLiteral(" "),
                                       MessageElementFlag::Text,
                                       MessageColor::System);
                b.emplace<TextElement>(QStringLiteral("[acknowledge]"),
                                       MessageElementFlag::Text,
                                       MessageColor(QColor(0, 200, 0)),
                                       FontStyle::ChatMediumBold)
                    ->setLink({Link::ChatWarnAcknowledge, channelId});
                tchan->addMessage(b.release(), MessageContext::Original);
            }
            else
            {
                channelPtr->addSystemMessage(text);
            }
        }
        if (!warnRecentlyIn(channelId))
        {
            acknowledgeWarningFor(channelId);
        }
    }
    return true;
}

// Observed live on chatrooms-user-v1 immediately after a ban/unban that
// affects the subscribed user. Payload uses capital ChannelID (not the
// lowercase channel_id convention of user_moderation_action).
bool handleAliasRestrictionUpdate(const QJsonObject &data, PubSubEvent &event)
{
    QString channelId = data.value(QStringLiteral("ChannelID")).toString();
    if (channelId.isEmpty())
    {
        channelId = data.value(QStringLiteral("channel_id")).toString();
    }
    if (channelId.isEmpty())
    {
        return false;
    }

    event.displayChannelId = channelId;
    const bool restricted =
        data.value(QStringLiteral("user_is_restricted")).toBool();
    event.displayText =
        restricted ? QStringLiteral("Alias restriction enabled in %1")
                         .arg(describeChannel(channelId))
                   : QStringLiteral("Alias restriction disabled in %1")
                         .arg(describeChannel(channelId));
    return true;
}

// community-points-user-v1 (client.js doc comment L36-46):
//   {"type":"points-spent",
//    "data":{"timestamp":,"balance":{"user_id","channel_id","balance"}}}
bool handlePointsSpent(const QJsonObject &payload, PubSubEvent &event)
{
    const QString type = payload[QStringLiteral("type")].toString();
    if (type != QLatin1String("points-spent"))
    {
        return false;  // anything else: generic events-channel line
    }

    const QJsonObject data = payload[QStringLiteral("data")].toObject();
    const QJsonObject balance = data[QStringLiteral("balance")].toObject();
    const int newBalance = balance[QStringLiteral("balance")].toInt();
    event.displayText = QStringLiteral("channel points balance is now %1")
                            .arg(newBalance);
    return true;
}

// predictions-user-v1 (newpubsubhermesreference/hermes/events/prediction.js).
// User topic types: "event-created", "event-updated", "prediction-result".
// event-created/-updated read data.event; prediction-result reads
// data.prediction {event_id, points, result:{type}}.

QString sharedPredictionOutcomeText(const QJsonObject &event)
{
    const QJsonArray outcomes = event[QStringLiteral("outcomes")].toArray();
    QString opts;
    for (const QJsonValue &v : outcomes)
    {
        if (!opts.isEmpty())
        {
            opts += QStringLiteral(" / ");
        }
        opts += v.toObject()[QStringLiteral("title")].toString();
    }
    return opts;
}

bool handleUserPredictionEvent(const QJsonObject &data, const QString &type,
                               PubSubEvent &event)
{
    const QJsonObject ev = data[QStringLiteral("event")].toObject();
    const QString title = ev[QStringLiteral("title")].toString();
    if (title.isEmpty())
    {
        return false;
    }
    const QString opts = sharedPredictionOutcomeText(ev);
    if (type == QLatin1String("event-created"))
    {
        event.displayText =
            opts.isEmpty()
                ? QStringLiteral("your prediction event started: %1").arg(title)
                : QStringLiteral("your prediction event started: %1 [%2]")
                      .arg(title, opts);
        return true;
    }
    if (type == QLatin1String("event-updated"))
    {
        const QString status = ev[QStringLiteral("status")].toString();
        event.displayText =
            opts.isEmpty()
                ? QStringLiteral("your prediction event: %1 (%2)")
                      .arg(title, status)
                : QStringLiteral("your prediction event: %1 (%2) [%3]")
                      .arg(title, status, opts);
        return true;
    }
    return false;
}

// Logs each unknown prediction-user notification shape once per type string,
// so a repeating unhandled event doesn't spam the debug log.
bool unhandledPredictionUserNotification(const QString &type)
{
    static QSet<QString> loggedOnce;
    if (!loggedOnce.contains(type))
    {
        loggedOnce.insert(type);
        qCDebug(chatterinoLiveupdates) << "Hermes predictions-user-v1:"
                                           " unhandled notification type"
                                       << type;
    }
    return false;
}

bool handlePredictionResult(const QJsonObject &data, PubSubEvent &event)
{
    // prediction-result handler (prediction.js L443-450):
    //   const { event_id: predictionId, points, result } = msg.data.prediction;
    const QJsonObject prediction = data[QStringLiteral("prediction")].toObject();
    const QJsonObject result = prediction[QStringLiteral("result")].toObject();
    const QString resultType = result[QStringLiteral("type")].toString();
    const int points = prediction[QStringLiteral("points")].toInt();

    if (resultType == QLatin1String("WIN"))
    {
        event.displayText =
            QStringLiteral("your prediction won: +%1 points").arg(points);
    }
    else if (resultType == QLatin1String("LOSE"))
    {
        event.displayText =
            QStringLiteral("your prediction lost: -%1 points").arg(points);
    }
    else if (resultType == QLatin1String("REFUND"))
    {
        event.displayText = QStringLiteral(
            "your prediction was cancelled; %1 points refunded")
                                .arg(points);
    }
    else
    {
        return unhandledPredictionUserNotification(resultType);
    }
    return true;
}

}  // namespace

void ensureHermesUserTopics()
{
    QString err;
    auto token = LimerinoAuth::resolveCurrentUserToken(&err);
    if (!token.hasToken())
    {
        return;  // the controller's never-submitted AuthBlocked path covers it
    }

    auto *c = getPubSubController();
    const QString &uid = token.userId;
    c->ensureTopic(QStringLiteral("chatrooms-user-v1.%1").arg(uid),
                   PubSubTopicAuth::User);
    c->ensureTopic(QStringLiteral("community-points-user-v1.%1").arg(uid),
                   PubSubTopicAuth::User);
    c->ensureTopic(QStringLiteral("predictions-user-v1.%1").arg(uid),
                   PubSubTopicAuth::User);
    // P3: decided to include the follows topic (reference USER_SUBS, L110).
    c->ensureTopic(QStringLiteral("follows.%1").arg(uid), PubSubTopicAuth::User);
}

void acknowledgeWarningManually(const QString &channelId)
{
    QString err;
    auto token = LimerinoAuth::resolveCurrentUserToken(&err);
    if (!token.hasToken())
    {
        return;
    }
    LimerinoAuth::gql::executePersisted(
        LimerinoAuth::gql::PQ_ACKNOWLEDGE_CHAT_WARNING,
        QJsonObject{{QStringLiteral("input"),
                     QJsonObject{{QStringLiteral("channelID"), channelId}}}},
        token.token, [](const QJsonObject & /*data*/) {},
        [](const auto & /*error*/) {});
}

void installHermesUserTopicHandlers(LimerinoPubSubController &controller)
{
    controller.registerTopicHandler(
        QStringLiteral("chatrooms-user-v1."),
        [](const QString & /*topic*/, const QJsonObject &payload,
           PubSubEvent &event) {
            const QString type =
                payload[QStringLiteral("type")].toString();
            if (type == QLatin1String("user_moderation_action"))
            {
                return handleUserModerationAction(
                    payload[QStringLiteral("data")].toObject(), event);
            }
            if (type == QLatin1String("channel_banned_alias_restriction_update"))
            {
                return handleAliasRestrictionUpdate(
                    payload[QStringLiteral("data")].toObject(), event);
            }
            return false;
        });

    controller.registerTopicHandler(
        QStringLiteral("community-points-user-v1."),
        [](const QString & /*topic*/, const QJsonObject &payload,
           PubSubEvent &event) {
            return handlePointsSpent(payload, event);
        });

    // user_moderation_action (events.js L21); referenced action trust comes
    // from the subscription (chatrooms-user-v1.USERID) and is only acknowledged.
    controller.registerKnownEventType(QStringLiteral("user_moderation_action"));
    controller.registerKnownEventType(
        QStringLiteral("channel_banned_alias_restriction_update"));

    // predictions-user-v1: real wire types per
    // newpubsubhermesreference/hermes/events/prediction.js. Replaces the
    // earlier guessed pre-registrations (prediction-event/-prediction), which
    // never matched live traffic.
    controller.registerKnownEventType(QStringLiteral("event-created"));
    controller.registerKnownEventType(QStringLiteral("event-updated"));
    controller.registerKnownEventType(QStringLiteral("prediction-result"));

    controller.registerTopicHandler(
        QStringLiteral("predictions-user-v1."),
        [](const QString & /*topic*/, const QJsonObject &payload,
           PubSubEvent &event) {
            const QString type =
                payload[QStringLiteral("type")].toString();
            const QJsonObject data =
                payload[QStringLiteral("data")].toObject();
            if (type == QLatin1String("event-created") ||
                type == QLatin1String("event-updated"))
            {
                return handleUserPredictionEvent(data, type, event);
            }
            if (type == QLatin1String("prediction-result"))
            {
                return handlePredictionResult(data, event);
            }
            return unhandledPredictionUserNotification(type);
        });

    // P3-addendum (user-selected): follows (client.js USER_SUBS L110 active).
    // events.js doesn't have a handler; reference shape appears in the
    // client.js docblock: type "user-followed" / "user-unfollowed",
    // payload {timestamp, target_display_name, target_username, target_user_id}.
    controller.registerKnownEventType(QStringLiteral("user-followed"));
    controller.registerKnownEventType(QStringLiteral("user-unfollowed"));
    controller.registerTopicHandler(
        QStringLiteral("follows."),
        [](const QString & /*topic*/, const QJsonObject &payload,
           PubSubEvent &event) {
            const QString type = payload[QStringLiteral("type")].toString();
            if (type == QLatin1String("user-followed"))
            {
                const QString name =
                    payload[QStringLiteral("target_display_name")].toString();
                event.displayText =
                    QStringLiteral("%1 followed").arg(
                        name.isEmpty()
                            ? payload[QStringLiteral("target_username")]
                                  .toString()
                            : name);
                return true;
            }
            if (type == QLatin1String("user-unfollowed"))
            {
                // unfollowed shape omits the display name (client.js L49-69):
                // use the raw user id.
                event.displayText = QStringLiteral("user unfollowed (%1)")
                                        .arg(payload[QStringLiteral(
                                                          "target_user_id")]
                                                 .toString());
                return true;
            }
            return false;
        });
}

}  // namespace chatterino::limerino
