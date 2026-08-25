// SPDX-License-Identifier: MIT
// Hermes topic name tables and parsing helpers.
//
// Topic strings are transcribed from pubsubreference/client.js
// (USER_SUBS L108-120, CHANNEL_SUBS L121-140 - including entries the
// reference lists but leaves commented out). Never invent topic strings.

#pragma once

#include <QString>

namespace chatterino::limerino {

// --- user topics (subscribe with "<name>.<userId>", client.js L154, L163) ---
inline const QString TOPIC_CHATROOMS_USER_V1 = QStringLiteral("chatrooms-user-v1");
inline const QString TOPIC_COMMUNITY_POINTS_USER_V1 =
    QStringLiteral("community-points-user-v1");
inline const QString TOPIC_PREDICTIONS_USER_V1 =
    QStringLiteral("predictions-user-v1");

// --- channel topics (subscribe with "<name>.<channelId>", client.js L157) ---
inline const QString TOPIC_PREDICTIONS_CHANNEL_V1 =
    QStringLiteral("predictions-channel-v1");
inline const QString TOPIC_POLLS = QStringLiteral("polls");
inline const QString TOPIC_RAID = QStringLiteral("raid");

/// Reference's channel-id extraction: topicString.split('.').pop()
/// (client.js L182) - the last '.'-separated segment of the topic string.
QString hermesTopicSuffix(const QString &topic);

/// User-topic prefixes known so far (used by the account-switch sweep to
/// parse the owning user id back out of a topic string). Returns the user id
/// suffix, or an empty string if the topic matches no known user prefix.
QString userIdFromUserTopic(const QString &topic);

}  // namespace chatterino::limerino
