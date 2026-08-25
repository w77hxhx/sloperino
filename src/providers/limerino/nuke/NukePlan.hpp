// SPDX-License-Identifier: MIT
// Value types for the nuke plan (batch N2).
//
// A NukePlan is the pure output of NukeEngine::buildPlan(): it carries every
// deduplicated moderation target and, for delete-style actions, the per-message
// ID list. The dialog (N3) executes this plan through the rate limiter; the
// engine itself never makes network calls.

#pragma once

#include "util/RapidjsonHelpers.hpp"
#include "util/RapidJsonSerializeQString.hpp"

#include <pajlada/serialize.hpp>
#include <QDateTime>
#include <QList>
#include <QString>
#include <QStringList>

namespace chatterino::limerino {

/// The single action a nuke applies to its matched scope. `DeleteAndTimeout`
/// is a convenience wrapper: every matched message is deleted, and every
/// matched sender is timed out (durations are configured at dialog time).
enum class NukeAction {
    Ban,
    Timeout,
    Warn,
    Delete,
    DeleteAndTimeout,
};

/// One moderation target for ban / timeout / warn. Multiple messages from the
/// same sender collapse into a single NukeTarget.
struct NukeTarget {
    QString userId;       // empty when the platform buffer cannot supply one
    QString login;        // lowercased login
    QString displayName;
    int matchedMessages = 0;
};

struct NukePlan {
    int messagesScanned = 0;
    int messagesMatched = 0;
    /// Actual time coverage of the in-memory buffer, for display. Empty if
    /// the channel buffer was empty.
    QDateTime bufferOldest;
    QDateTime bufferNewest;
    /// True when the requested lookback is longer than the buffered history.
    /// The dialog must flag this prominently: a 10-minute lookback may only
    /// be able to reach back seconds in a busy channel.
    bool lookbackExceedsBuffer = false;
    /// Per-sender targets. Populated for Ban / Timeout / Warn / DeleteAndTimeout.
    QList<NukeTarget> targets;
    /// Per-message IDs. Populated for Delete / DeleteAndTimeout.
    QStringList messageIds;
    /// Non-fatal observations (empty buffer, messages without IDs skipped, etc).
    QStringList warnings;
};

}  // namespace chatterino::limerino

namespace pajlada {

template <>
struct Serialize<chatterino::limerino::NukeTarget> {
    static rapidjson::Value get(
        const chatterino::limerino::NukeTarget &value,
        rapidjson::Document::AllocatorType &a);
};

template <>
struct Deserialize<chatterino::limerino::NukeTarget> {
    static chatterino::limerino::NukeTarget get(const rapidjson::Value &value,
                                                bool *error = nullptr);
};

}  // namespace pajlada
