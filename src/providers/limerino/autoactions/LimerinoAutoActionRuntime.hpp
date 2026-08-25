// SPDX-License-Identifier: MIT
// Runtime evaluation of auto-action rules against an incoming message
// (batch N6).
//
// One entry point, safe to call from the GUI thread on every message that
// lands in a channel. All safety controls live here and are not optional:
//   * never act on the current user's own messages (unconditional),
//   * per-rule cooldown (rule.cooldownSeconds),
//   * no re-entry (a re-entrant call short-circuits),
//   * moderator check per channel before firing,
//   * unknown / unavailable ID-valued placeholders skip the action (expand
//     returns std::nullopt), logged once per rule.

#pragma once

#include <QString>

namespace chatterino {

class Channel;
struct Message;
using MessagePtr = std::shared_ptr<const Message>;

}  // namespace chatterino

namespace chatterino::limerino {

/// Fire any auto-action rules that match the just-arrived message. The caller
/// filters the candidate messages itself: only original, non-system, non-
/// whisper chat messages are evaluated. This function is a no-op when the
/// channel key resolves to no applicable rules and cheap when it resolves to
/// none (cached shared vector lookup only).
void evaluateAutoActions(const MessagePtr &message, Channel &channel);

/// Clears all per-rule cooldown state. Wired to the controller's
/// rulesChanged; manual for tests.
void LimerinoAutoActionRuntime_resetCooldowns();

}  // namespace chatterino::limerino
