// SPDX-License-Identifier: MIT
// Unauthenticated Hermes channel topics (batch P1).
//
// Topic strings transcribed from pubsubreference/client.js. Event-type
// strings / payload field paths transcribed from pubsubreference/events.js
// where present; anything not in the reference is marked as such in the cpp.

#pragma once

#include <QString>

namespace chatterino {

class TwitchChannel;

namespace limerino {

class LimerinoPubSubController;

/// Registers the per-channel Hermes topics (raid, polls, predictions) from
/// this given open Twitch channel. Idempotent per channel - the controller
/// dedupes by topic string.
void ensureHermesChannelTopics(const TwitchChannel &channel);

/// One-time pipeline wiring: hook topic handlers into `controller`.
/// Called once from `initializePubSub()`.
void installHermesChannelTopicHandlers(LimerinoPubSubController &controller);

}  // namespace limerino
}  // namespace chatterino
