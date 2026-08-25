// SPDX-License-Identifier: MIT
// Persistence for nuke presets (batch N4). Reads/writes the
// `/limerino/nukePresets` QStringSetting. Presets are edited exclusively
// through the nuke dialog; these helpers are the only call sites that touch
// the raw setting.

#pragma once

#include <QString>
#include <QVector>

namespace chatterino::limerino {

struct LimerinoNukePreset;

/// All saved presets, in stored order. Malformed entries are dropped.
QVector<LimerinoNukePreset> loadNukePresets();
void saveNukePresets(const QVector<LimerinoNukePreset> &presets);

/// Append or replace (matched by name, case-insensitive).
void upsertNukePreset(const LimerinoNukePreset &preset);

/// Remove by name, case-insensitive. No-op when missing.
void eraseNukePreset(const QString &name);

}  // namespace chatterino::limerino
