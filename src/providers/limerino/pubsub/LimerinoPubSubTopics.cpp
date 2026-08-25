// SPDX-License-Identifier: MIT

#include "providers/limerino/pubsub/LimerinoPubSubTopics.hpp"

#include <array>

namespace chatterino::limerino {

namespace {

// Known user topics: active + reference-listed (client.js L108-120).
// Extend when batches port more user topics.
const std::array USER_TOPIC_NAMES = {
    "chatrooms-user-v1",
    "community-points-user-v1",
    "predictions-user-v1",
    "presence",
};

}  // namespace

QString hermesTopicSuffix(const QString &topic)
{
    const qsizetype idx = topic.lastIndexOf(QLatin1Char('.'));
    if (idx < 0 || idx + 1 >= topic.size())
    {
        return topic;
    }
    return topic.sliced(idx + 1);
}

QString userIdFromUserTopic(const QString &topic)
{
    for (const auto *name : USER_TOPIC_NAMES)
    {
        const QString prefix = QLatin1String(name) + QLatin1Char('.');
        if (topic.startsWith(prefix))
        {
            return topic.sliced(prefix.size());
        }
    }
    return {};
}

}  // namespace chatterino::limerino
