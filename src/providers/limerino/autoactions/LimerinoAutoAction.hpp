// SPDX-License-Identifier: MIT
// An auto-action rule (batch N5).
//
// One rule = "when a chat message matching (content ∧ sender) arrives in a
// channel inside `scope`, run `action` as if it had been typed in chat." All
// dispatch goes through CommandController::execCommand, so built-in, Limerino,
// and user-defined commands all work.
//
// The matcher semantics are exactly the nuke's: empty matcher ⇒ wildcard,
// both-empty is rejected by the editor (N1 validator); the action string is
// a command template with a small placeholder namespace handled in N6.
//
// Scope model follows the highlight-group model: AllExcept (empty == everywhere)
// or Only (only the listed channels), channels normalised to lowercased
// "platform:name" keys (see HighlightGroupChannelKey.hpp).

#pragma once

#include "providers/limerino/matcher/LimerinoMatcher.hpp"

#include "util/RapidjsonHelpers.hpp"
#include "util/RapidJsonSerializeQString.hpp"

#include <pajlada/serialize.hpp>
#include <QSet>
#include <QString>
#include <QStringList>
#include <QUuid>

namespace chatterino::limerino {

struct AutoActionContext;

struct LimerinoAutoAction {
    QUuid id = QUuid::createUuid();
    QString name;                   // user-facing, for the editor + logs
    bool enabled = true;

    LimerinoMatcher content;        // message text
    LimerinoMatcher sender;         // sender login

    enum class Scope { AllExcept, Only };
    Scope scope = Scope::AllExcept;
    QStringList channels;           // normalised lowercase "platform:name" keys

    QString action;                 // command template, e.g. "/ban {sender.name}"

    int cooldownSeconds = 10;       // per-rule cooldown

    // --- cached, derived ---
    QSet<QString> channelSet;       // rebuilt from channels at load

    bool operator==(const LimerinoAutoAction &other) const;

    /// Same semantics as HighlightGroup::matches: `AllExcept` applies
    /// everywhere except `channels` (empty = everywhere), `Only` applies only
    /// in `channels`. Both are case-insensitive on the channel key.
    bool matchesChannel(const QString &channelKey) const;

    /// Normalisers used by read/write:
    ///  - trim/lowercase/dedupe `channels`, rebuilding `channelSet`.
    ///  - clamp cooldownSeconds to >= 0 (0 = every message).
    void normalize();
};

}  // namespace chatterino::limerino

namespace pajlada {

template <>
struct Serialize<chatterino::limerino::LimerinoAutoAction> {
    static rapidjson::Value get(
        const chatterino::limerino::LimerinoAutoAction &value,
        rapidjson::Document::AllocatorType &a);
};

template <>
struct Deserialize<chatterino::limerino::LimerinoAutoAction> {
    static chatterino::limerino::LimerinoAutoAction get(
        const rapidjson::Value &value, bool *error = nullptr);
};

}  // namespace pajlada
