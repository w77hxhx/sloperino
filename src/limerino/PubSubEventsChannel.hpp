// SPDX-License-Identifier: MIT
// "/events" - special channel (like /mentions or /whispers) that
// receives every live event. Per-event-type visibility is
// user-controlled; filters apply to newly arriving events.

#pragma once

#include "common/Channel.hpp"

#include <QString>
#include <QStringList>

#include <optional>

namespace chatterino::limerino {

/// Routed in TwitchIrcServer::getCustomChannel so tabs/splits/startup
/// persistence resolve by name, exactly like /mentions.
const QString &pubSubEventsChannelName();

/// GUI thread; created on first call.
ChannelPtr pubSubEventsChannel();

/// Opens the events channel in a new tab of the main window.
void openPubSubEventsChannelTab();

// --- per-event-type visibility filter ---
bool pubSubEventTypeHidden(const QString &type);
void setPubSubEventTypeHidden(const QString &type, bool hidden);
QStringList hiddenPubSubEventTypes();

/// Compact raw Hermes payload retained for /events messages (E1.d).
std::optional<QString> rawEventPayloadCompact(const QString &messageId);

}  // namespace chatterino::limerino
