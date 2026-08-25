// SPDX-License-Identifier: MIT
// One matcher (content or sender) shared by the nuke and by auto actions.
//
// Semantics (locked, do not drift):
//  - an empty pattern matches EVERYTHING and is treated as an absent
//    constraint, not an empty-string match;
//  - an invalid regex never falls back to substring matching; it surfaces via
//    compileError(), makes matches() return false, and is rejected by
//    limerino::validateMatcherPair;
//  - case-insensitive by default, with a per-matcher toggle mirroring
//    HighlightPhrase's isRegex/isCaseSensitive pair.
//
// This is a pure value type: no QObject, no settings, no app state. It is
// constructed once per rule/preset and its regex is precompiled there, never
// per message.

#pragma once

#include "util/RapidjsonHelpers.hpp"
#include "util/RapidJsonSerializeQString.hpp"

#include <pajlada/serialize.hpp>
#include <QRegularExpression>
#include <QString>
#include <QStringView>

#include <optional>

namespace chatterino::limerino {

struct LimerinoMatcher {
    QString pattern;           // empty == matches anything
    bool caseSensitive = false;
    bool isRegex = true;
    QRegularExpression compiled;  // precompiled at construction
    QString literalNeedle;        // pre-lowered when !caseSensitive (literal mode)

    LimerinoMatcher() = default;
    LimerinoMatcher(QString pattern, bool isCaseSensitive, bool isRegex);

    bool matches(QStringView text) const;  // empty pattern -> true
    bool isEmpty() const
    {
        return this->pattern.isEmpty();
    }
    /// Compile error text, if isRegex and the pattern failed to compile.
    std::optional<QString> compileError() const;

    /// Force recompilation of `compiled` / `literalNeedle` after the public
    /// fields have been mutated directly. Call this after settings round-trip.
    void normalize();

private:
    void rebuild();
};

}  // namespace chatterino::limerino

namespace pajlada {

template <>
struct Serialize<chatterino::limerino::LimerinoMatcher> {
    static rapidjson::Value get(
        const chatterino::limerino::LimerinoMatcher &value,
        rapidjson::Document::AllocatorType &a);
};

template <>
struct Deserialize<chatterino::limerino::LimerinoMatcher> {
    static chatterino::limerino::LimerinoMatcher get(
        const rapidjson::Value &value, bool *error = nullptr);
};

}  // namespace pajlada
