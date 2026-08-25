// SPDX-License-Identifier: MIT
// Limerino theme creator: the user-editable seed. Everything outside this
// struct is derived (see LimerinoThemeGenerator.{hpp,cpp}).
//
// The generator is a pure function seed -> QJsonObject. No singletons, no
// Application, no getTheme(), no Settings - unit-testable without a mock app.

#pragma once

#include <QColor>
#include <QJsonObject>
#include <QString>
#include <QStringList>

namespace chatterino::limerino {

/// The four user-chosen inputs to the theme creator. Every other leaf of the
/// generated theme JSON derives from these.
struct LimerinoThemeSeed {
    QColor background;  ///< base surface: window + splits background
    QColor surface;     ///< secondary surface: tabs, split header, input
    QColor accent;      ///< accent + selected tab line + focused borders
    QColor text;        ///< primary text color

    /// WCAG 2.0 relative luminance (0..1) of the background. All derived
    /// logic keys off this one value so the classification lives in one place.
    double backgroundLuminance() const;

    /// Luminance midpoint on the relative-luminance scale; named and reused
    /// so tests pin the rule.
    bool isLight() const;

    /// Preset seeds approximating the built-in Dark / Light themes; the
    /// dialog's reset buttons use these.
    static LimerinoThemeSeed darkPreset();
    static LimerinoThemeSeed lightPreset();
};

/// WCAG 2.0 contrast ratio between two colors, 1.0..21.0.
double contrastRatio(const QColor &a, const QColor &b);

}  // namespace chatterino::limerino
