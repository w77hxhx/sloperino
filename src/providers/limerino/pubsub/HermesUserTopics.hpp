// SPDX-License-Identifier: MIT
// Authenticated Hermes *user* topics (batch P2): chatrooms-user-v1,
// community-points-user-v1, predictions-user-v1.
//
// Topic strings from pubsubreference/client.js (USER_SUBS L108-120);
// event shapes from pubsubreference/events.js (chatrooms-user-v1 handler)
// and the client.js header comment (community-points-user-v1 points-spent
// shape). predictons-user-v1 has no reference events - its payloads pass
// through to the events channel unparsed.
//
// All three topics need the extra-features auth (PubSubTopicAuth::User); the
// controller blocks them when no account matches the primary login.
//
// Gating conditions the caller must honor:
//  - no topic is ever built for a user id we don't currently authenticate as
//  - account add/remove/switch re-runs registration via the controller's
//    reconcile; nothing here caches a token.

#pragma once

#include <QString>

namespace chatterino::limerino {

class LimerinoPubSubController;

/// Ensures all three user topics (chatrooms-user-v1.<uid>,
/// community-points-user-v1.<uid>, predictions-user-v1.<uid>) are registered,
/// resolving <uid> through the extra-features auth right now.
void ensureHermesUserTopics();

/// One-time registration of the topic handlers. Called by initializePubSub()
/// *after* the controller exists.
void installHermesUserTopicHandlers(LimerinoPubSubController &controller);

/// One-shot manual acknowledge entry (warn dialog link / user action).
/// The setting-gated pubsub variant calls the same code path; no auto-ack
/// happens without the setting.
void acknowledgeWarningManually(const QString &channelId);

}  // namespace chatterino::limerino
