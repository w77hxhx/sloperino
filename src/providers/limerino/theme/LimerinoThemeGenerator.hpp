// SPDX-License-Identifier: MIT
// Limerino theme creator: seed + a real base theme JSON.
// Recolor walks the base (Dark.json / Light.json / …) and replaces only
// colours that match the base seed; every other leaf stays as in the file.

#pragma once

#include <QColor>
#include <QJsonObject>
#include <QString>
#include <QStringList>

#include <optional>

namespace chatterino::limerino {

struct LimerinoThemeSeed;

/// Built-in Chatterino themes shipped in `:/themes/<Name>.json`.
enum class BuiltinTheme {
    Dark,
    Light,
    Black,
    White,
};

QString builtinThemeName(BuiltinTheme builtin);

/// Load a built-in theme JSON from Qt resources. Nullopt if the qrc is missing
/// (e.g. some test binaries).
std::optional<QJsonObject> loadBuiltinThemeJson(BuiltinTheme builtin);

/// Pull the four seed colours from known paths in a full theme JSON.
LimerinoThemeSeed seedFromThemeJson(const QJsonObject &theme);

/// Copy `base` and replace colours whose RGB matches `from` with `to`
/// (original alpha is kept). Non-colour strings such as "transparent" stay.
QJsonObject recolorTheme(const QJsonObject &base, const LimerinoThemeSeed &from,
                         const LimerinoThemeSeed &to);

/// Recolor the Dark or Light built-in (by `seed.isLight()`). Falls back to a
/// synthesized theme only when the built-in JSON cannot be loaded.
QJsonObject generateTheme(const LimerinoThemeSeed &seed);

/// Recolor an explicit base document, then stamp the public Limerino `$schema`
/// and iconTheme from `seed`.
QJsonObject generateThemeFromBase(const QJsonObject &base,
                                  const LimerinoThemeSeed &baseSeed,
                                  const LimerinoThemeSeed &seed);

/// Pairs the generator can't make readable at low contrast, one line per
/// violation. Empty list = no warnings. Does not mutate the output.
QStringList contrastWarnings(const LimerinoThemeSeed &seed);

}  // namespace chatterino::limerino
