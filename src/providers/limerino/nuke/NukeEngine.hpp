// SPDX-License-Identifier: MIT
// Pure nuke plan builder (batch N2).
//
// buildPlan() walks the channel's message snapshot and produces a NukePlan.
// It performs no network calls, no settings access, no QObject lifetime
// concerns — only the snapshot read. The same function is used by the
// dialog's Preview and by Execute (Execute re-derives a fresh plan at the
// moment of execution, so the plan cannot go stale between preview and run).

#pragma once

#include "messages/Message.hpp"
#include "providers/limerino/nuke/NukePlan.hpp"

#include <QDateTime>
#include <QString>

#include <optional>
#include <vector>

namespace chatterino {

class Channel;

}

namespace chatterino::limerino {

struct LimerinoMatcher;

/// Request-level validation, applied before any snapshot is touched. Returns
/// an error message, or std::nullopt when the request is legal.
///
/// Unlike validateMatcherPair this also rejects a non-positive lookback.
std::optional<QString> validateNukeRequest(const LimerinoMatcher &content,
                                           const LimerinoMatcher &sender,
                                           int lookbackSeconds);

/// Build a nuke plan over a message snapshot.
///
/// Pure function over the snapshot: no network, no settings, no QObject. The
/// dialog calls this both for Preview and for Execute (re-deriving a fresh
/// snapshot at the moment of execution, so the plan can never go stale).
///
/// Only real chat messages are considered: system messages, whisper copies,
/// timeout/deletion marker records, moderation-action records, and already
/// deleted (Disabled-flag) messages are excluded. The current user and the
/// broadcaster are never targets.
///
/// @p selfLogin / @p channelBroadcasterLogin are supplied by the caller so
///        this function does not touch Application state (and so unit tests
///        can drive it without a mock app).
NukePlan buildPlan(const std::vector<MessagePtr> &snapshot,
                   MessagePlatform platform, const QString &channelName,
                   const QString &selfLogin, const LimerinoMatcher &content,
                   const LimerinoMatcher &sender, int lookbackSeconds,
                   NukeAction action,
                   const QDateTime &now = QDateTime::currentDateTime());

/// Small GUI-facing helper used by the dialog (N3): current operator's
/// lowercased login for the channel's platform, or empty if not logged in.
/// Lives here (not in the engine body) so buildPlan stays free of app state.
QString nukeSelfLogin(const Channel &channel);

}  // namespace chatterino::limerino
