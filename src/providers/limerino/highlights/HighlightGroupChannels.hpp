// SPDX-License-Identifier: MIT
// Helper that collects the set of channel keys the client currently knows
// about ("currently open and recently joined" channels). Used by the H3
// channel-list editor for autocomplete + typo-warning.
//
// Sources:
//   * every Split in every open window (the UI-truth, no provider coupling);
//   * all historical entries the groups already reference (stale entries are
//     preserved as keys even if no longer present, so they get a warning but
//     are not silently dropped).

#pragma once

#include <QSet>
#include <QString>

namespace chatterino::limerino {

/// Returns every channel key currently known to the client.
/// Keys are lowercased, "platform:name" form (same normalisation as
/// HighlightGroup / highlightChannelKey).
QSet<QString> knownHighlightChannelKeys();

}  // namespace chatterino::limerino
