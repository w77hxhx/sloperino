// SPDX-License-Identifier: MIT
// A saved nuke configuration (matchers + lookback + action + params).
// Stored under /limerino/nukePresets as a JSON array (batch N4).
// Deleting presets is immediate; loading one repopulates the dialog.

#pragma once

#include "providers/limerino/matcher/LimerinoMatcher.hpp"
#include "providers/limerino/nuke/NukePlan.hpp"

#include "util/RapidjsonHelpers.hpp"
#include "util/RapidJsonSerializeQString.hpp"

#include <pajlada/serialize.hpp>
#include <QString>

namespace chatterino::limerino {

struct LimerinoNukePreset {
    QString name;
    LimerinoMatcher content;
    LimerinoMatcher sender;
    int lookbackSeconds = 600;
    NukeAction action = NukeAction::Ban;
    int timeoutSeconds = 600;   // only meaningful for Timeout/DeleteAndTimeout
    QString reason;             // for Ban/Warn/Timeout; may be empty

    bool operator==(const LimerinoNukePreset &other) const;
};

}  // namespace chatterino::limerino

namespace pajlada {

template <>
struct Serialize<chatterino::limerino::LimerinoNukePreset> {
    static rapidjson::Value get(
        const chatterino::limerino::LimerinoNukePreset &value,
        rapidjson::Document::AllocatorType &a);
};

template <>
struct Deserialize<chatterino::limerino::LimerinoNukePreset> {
    static chatterino::limerino::LimerinoNukePreset get(
        const rapidjson::Value &value, bool *error = nullptr);
};

}  // namespace pajlada
