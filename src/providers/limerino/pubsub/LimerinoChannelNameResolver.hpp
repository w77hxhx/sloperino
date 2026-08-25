// SPDX-License-Identifier: MIT
// Session-scoped channel/user id -> login resolver for /events display text.
// Lookup order: open Twitch channels, LimerinoAuth moderated-channel lists,
// session cache; on miss, coalesce ids briefly and GET /helix/users through
// LimerinoRateLimiter. Callbacks always run on the GUI thread.

#pragma once

#include <QString>

#include <functional>

namespace chatterino::limerino {

using ChannelNameResolvedCallback =
    std::function<void(const QString &displayName)>;

/// Sync probe: fills `outName` and returns true when a name is already known.
/// Never starts a network request.
bool trySyncChannelName(const QString &id, QString &outName);

/// Resolve `id` to a login (preferred) or display name. Invokes `cb`
/// immediately on a sync hit; otherwise after a coalesced Helix batch.
/// On total failure `cb` still runs with `id` so callers can proceed.
void resolveChannelName(const QString &id, ChannelNameResolvedCallback cb);

}  // namespace chatterino::limerino
