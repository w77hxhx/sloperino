// SPDX-License-Identifier: MIT

#include "providers/limerino/theme/LimerinoThemeSeed.hpp"

#include <cmath>

namespace chatterino::limerino {

namespace {

/// WCAG 2.0 relative luminance of one sRGB channel value (0..1, linearized).
double channelLinear(double srgb)
{
    return srgb <= 0.03928 ? srgb / 12.92
                           : std::pow((srgb + 0.055) / 1.055, 2.4);
}

double relativeLuminance(const QColor &color)
{
    return 0.2126 * channelLinear(color.redF()) +
           0.7152 * channelLinear(color.greenF()) +
           0.0722 * channelLinear(color.blueF());
}

/// Perceptual midpoint of the WCAG luminance scale. Colors at or above read
/// as "light" and get dark icons; below reads as "dark".
constexpr double LIGHT_LUMINANCE_FLOOR = 0.179;

}  // namespace

double LimerinoThemeSeed::backgroundLuminance() const
{
    return relativeLuminance(this->background);
}

bool LimerinoThemeSeed::isLight() const
{
    return this->backgroundLuminance() >= LIGHT_LUMINANCE_FLOOR;
}

LimerinoThemeSeed LimerinoThemeSeed::darkPreset()
{
    LimerinoThemeSeed s;
    s.background = QColor(QStringLiteral("#191919"));
    s.surface = QColor(QStringLiteral("#2e2e2e"));
    s.accent = QColor(QStringLiteral("#00aeef"));
    s.text = QColor(QStringLiteral("#ffffff"));
    return s;
}

LimerinoThemeSeed LimerinoThemeSeed::lightPreset()
{
    LimerinoThemeSeed s;
    s.background = QColor(QStringLiteral("#f5f5f5"));
    s.surface = QColor(QStringLiteral("#ffffff"));
    s.accent = QColor(QStringLiteral("#00aeef"));
    s.text = QColor(QStringLiteral("#000000"));
    return s;
}

double contrastRatio(const QColor &a, const QColor &b)
{
    const double lumA = relativeLuminance(a);
    const double lumB = relativeLuminance(b);
    const double lighter = std::max(lumA, lumB);
    const double darker = std::min(lumA, lumB);
    return (lighter + 0.05) / (darker + 0.05);
}

}  // namespace chatterino::limerino
