// SPDX-License-Identifier: MIT
// Helpers that produce the canonical channel key used by highlight groups.
// Format: "<platform>:<name>" lowercased, e.g. "twitch:forsen", "kick:someone".
// Non-chat channels (whispers, mentions, live, automod, misc) get sentinel keys
// like "special:whispers" so they naturally receive every AllExcept group and
// no Only group unless explicitly listed.

#pragma once

#include <QString>

#include <cstdint>

namespace chatterino {

class Channel;
// Forward-declared here to avoid pulling messages/Message.hpp into headers.
enum class MessagePlatform : uint8_t;

namespace limerino {

/// Key from a live Channel object. Uses Channel::messagePlatform() and
/// Channel::getType() to produce a platform-qualified, lowercased key.
QString highlightChannelKey(const Channel &channel);

/// Key from explicit platform + name (for call sites that lack a Channel*).
QString highlightChannelKey(MessagePlatform platform, const QString &name);

}  // namespace limerino
}  // namespace chatterino
