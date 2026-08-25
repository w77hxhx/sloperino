// SPDX-License-Identifier: MIT
// Event identity + bounded LRU dedupe for /events (batch B4.1).
//
// Identity prefers natural keys from the payload (never rendered text):
//   raids:      type + raid.id
//   polls:      type + poll_id + status
//   predictions: type + event.id (+ status when present)
// Fallback: SHA-1 of compact JSON (types with no natural key).
//
// Windows: raid_update_v2 uses 120s (covers force_raid_now_seconds heartbeats);
// all other types use 30s. Cap prevents unbounded growth in long sessions.

#pragma once

#include <QJsonObject>
#include <QString>

#include <chrono>
#include <cstddef>
#include <vector>

namespace chatterino::limerino {

struct PubSubEvent;

/// Stable identity string for dedupe. Empty only if both type and payload are
/// empty (should not happen for real notifications).
QString pubSubEventIdentity(const PubSubEvent &event);

/// Window for this event type (milliseconds).
std::chrono::milliseconds pubSubDedupeWindowMs(const QString &eventType);

class PubSubEventDedupe
{
public:
    static constexpr std::size_t kMaxEntries = 256;

    /// Returns true if this identity was seen inside its window (caller should
    /// drop). Inserts/refreshes otherwise.
    bool isDuplicate(const QString &identity,
                     std::chrono::milliseconds window);

    void clear();

    std::size_t size() const;

private:
    struct Entry {
        QString identity;
        std::chrono::steady_clock::time_point expires;
    };

    void pruneExpired(std::chrono::steady_clock::time_point now);

    // Newest at the back; lookup is linear over <= kMaxEntries.
    std::vector<Entry> entries_;
};

PubSubEventDedupe &pubSubEventDedupe();

}  // namespace chatterino::limerino
