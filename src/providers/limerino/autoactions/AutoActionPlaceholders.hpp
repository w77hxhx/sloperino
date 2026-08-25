// SPDX-License-Identifier: MIT
// Placeholder expansion for auto-action command templates (shared by N5 tests
// and the N6 dispatch path).
//
// Placeholder namespace (lock-ins):
//   {msg.id}              message ID
//   {sender.name}         sender login (lowercase)
//   {sender.displayName}  display name
//   {sender.id}           sender user ID
//   {channel.name}        channel login (lowercase)
//   {channel.id}          channel ID (may be empty on special channels)
//   {platform}            "twitch" | "kick"
//
// NOTE: {msg.text} is *deliberately excluded* in v1. A chat message is
// attacker-controlled and would land raw in a command line; any future
// reintroduction must ship with an explicit sanitization step (see FORK.md).
//
// Rules:
//   * unknown placeholders expand to empty and log once (logOnceOnly=true),
//   * a literal "{{" yields "{", a literal "}}" yields "}",
//   * if a value is unavailable (empty id), the action is SKIPPED — never send
//     a command with an empty argument where an ID was expected.
//
// Batches:
//   N5: expand() with this exact interface, so the rules (not the transport)
//       can have unit tests.
//   N6: dispatch site uses expand() and the rules engine consults it
//       per-message.

#pragma once

#include <QString>

#include <optional>

namespace chatterino::limerino {

struct AutoActionContext {
    // All of these may be empty. The caller knows what a given channel
    // actually supports and populates accordingly.
    QString msgId;            // "" = action skipped when template uses {msg.id}
    QString senderLogin;      // lowercase login
    QString senderDisplayName;
    QString senderId;
    QString channelName;      // lowercase login
    QString channelId;        // "" when unavailable (e.g. special channels)
    QString platform;         // "twitch" | "kick" | other
};

/// Expand `templ` against `ctx`.
/// @param logUnknowns when true, unknown placeholders emit a single warning
///        per placeholder per call (the dispatch site passes false because it
///        handles its own "log-once" dedupe against each rule ID).
/// @returns the expanded command, or std::nullopt if a required value is
///          unavailable or the template resolved to an empty/blank string.
std::optional<QString> expandAutoAction(const QString &templ,
                                        const AutoActionContext &ctx,
                                        bool logUnknowns = false);

/// Test whether `templ` is "well-formed" — i.e. every placeholder in it is
/// known AND the channel context can supply everything it needs.
std::optional<QString> validateAutoActionTemplate(const QString &templ);

}  // namespace chatterino::limerino
