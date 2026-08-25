// SPDX-License-Identifier: MIT
// Single enforcement point for the matcher-pair rules shared by the nuke
// dialog and the auto-action editor:
//   * at least one of the two matchers must be non-empty (an all-empty pair
//     selects the whole buffer / every message — rejected here, not at
//     confirmation time);
//   * a matcher that claims to be a regex must compile.
//
// No Qt widgets, no settings — a pure helper used by both editors and by the
// NukeEngine entry points.

#pragma once

#include <QString>

#include <optional>

namespace chatterino::limerino {

struct LimerinoMatcher;

/// Returns an error message if the (content, sender) pair is invalid for use
/// as moderation criteria, std::nullopt otherwise. `fieldContentName` and
/// `fieldSenderName` are the UI labels, so the error text can name the field.
std::optional<QString> validateMatcherPair(
    const LimerinoMatcher &contentMatcher, const LimerinoMatcher &senderMatcher,
    const QString &fieldContentName = QStringLiteral("message"),
    const QString &fieldSenderName = QStringLiteral("sender"));

}  // namespace chatterino::limerino
