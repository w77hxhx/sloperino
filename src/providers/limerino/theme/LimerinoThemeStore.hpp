// SPDX-License-Identifier: MIT
// Limerino theme creator: file formats and the Themes-directory install path.
//
// Two distinct files share the T-series:
//   themes.json        - the FULL generated theme (Chatterino-conformant;
//                        `$schema` uses the public raw.githubusercontent URL
//                        so a hand-off of the file validates in editors).
//   limerino_theme.json - the seed wrapper
//                        { "limerinoThemeVersion": 1, "name", "seed" } - the
//                        creator's working file, never meant for Themes/.
//
// Font is not part of a Chatterino theme; the creator drops it on export and
// the dialog says so (see LimerinoThemeGenerator landing page).

#pragma once

#include "providers/limerino/theme/LimerinoThemeSeed.hpp"

#include <QJsonObject>
#include <QString>
#include <QStringList>

#include <optional>

namespace chatterino::limerino {

struct LimerinoThemeFile {
    QString name;
    LimerinoThemeSeed seed;
};

/// Serialize just the seed into the wrapper format.
QJsonObject serializeSeedFile(const QString &name, const LimerinoThemeSeed &seed);

/// True iff the root object looks like a seed wrapper (limerinoThemeVersion key).
bool isSeedFile(const QJsonObject &root);

/// Parse a seed wrapper. Returns nullopt when the input is a full theme JSON,
/// malformed, or has an unrecognized version. Error message in `err`.
std::optional<LimerinoThemeFile> parseSeedFile(const QJsonObject &root,
                                               QString *err);

/// Turn a user-facing name into a safe filename for Themes/<Name>.json.
/// Strips Windows-invalid characters and collapses whitespace.
QString sanitizeThemeFilename(const QString &name);

/// Install the generated full theme at Themes/<Name>.json and (on success)
/// select it via the theme-name setting. Returns whether selection succeeded.
bool installGeneratedTheme(const QString &name, const LimerinoThemeSeed &seed,
                           QString *err);

/// Write an already-built theme JSON to Themes/<Name>.json and select it.
bool installThemeJson(const QString &name, const QJsonObject &themeJson,
                      QString *err);

/// Copy an already-valid full-theme JSON file into Themes/ as-is. Used by
/// Import when the input is a themes.json (not reducible to a seed).
bool installFullThemeFile(const QString &sourcePath, const QString &name,
                          QString *err);

constexpr int kThemeRecentsLimit = 8;

/// MRU of installed theme filenames (with .json). Newest first.
QStringList loadThemeRecents();
void pushThemeRecent(const QString &filename);

}  // namespace chatterino::limerino
