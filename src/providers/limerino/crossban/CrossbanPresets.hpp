// SPDX-License-Identifier: MIT
// Crossban channel presets. JSON under /limerino/crossban/presets.
// Always includes a dynamic "Every moderated channel" sentinel whose channel
// list is resolved live from LimerinoAuth moderated lists (not snapshotted).

#pragma once

#include <QString>
#include <QUuid>
#include <QVector>

namespace chatterino::limerino {

struct CrossbanChannel {
    QString id;
    QString login;
    QString displayName;
};

struct CrossbanPreset {
    QUuid id;
    QString name;
    QVector<CrossbanChannel> channels;
    /// When true, `channels` is ignored at use-time; expand via
    /// `collectAllModeratedChannels()` instead.
    bool useAllModeratedChannels = false;
};

/// Stable id for the built-in dynamic default (persisted across sessions).
QUuid allModeratedChannelsPresetId();

CrossbanPreset makeAllModeratedChannelsPreset();

/// Union of moderated channels across LimerinoAuth accounts (deduped by id).
QVector<CrossbanChannel> collectAllModeratedChannels();

/// Ensure the dynamic default exists. Returns true if `presets` was mutated.
bool ensureAllModeratedPreset(QVector<CrossbanPreset> &presets);

/// Load + migrate (ensure dynamic default; fix null lastPresetId) + persist if needed.
QVector<CrossbanPreset> loadCrossbanPresets();
void saveCrossbanPresets(const QVector<CrossbanPreset> &presets);

QUuid lastCrossbanPresetId();
void setLastCrossbanPresetId(const QUuid &id);

}  // namespace chatterino::limerino
