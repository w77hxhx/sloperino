// SPDX-License-Identifier: MIT
// Pure-function tests for the Limerino theme creator. No app, no mock, no UI.

#include "providers/limerino/theme/LimerinoThemeGenerator.hpp"

#include "providers/limerino/theme/LimerinoThemeSeed.hpp"

#include <gtest/gtest.h>
#include <QJsonDocument>
#include <QJsonObject>

using namespace chatterino::limerino;

namespace {

// Every leaf in the documented T0(b) diff inventory plus the anchors. Keyed
// by dotted path so failures are readable.
const char *REQUIRED_LEAVES[] = {
    "colors.accent",
    "colors.window.background",
    "colors.window.text",
    "colors.tabs.dividerLine",
    "colors.tabs.liveIndicator",
    "colors.tabs.rerunIndicator",
    "colors.tabs.regular.text",
    "colors.tabs.regular.backgrounds.regular",
    "colors.tabs.regular.backgrounds.hover",
    "colors.tabs.regular.backgrounds.unfocused",
    "colors.tabs.regular.line.regular",
    "colors.tabs.newMessage.text",
    "colors.tabs.newMessage.line.regular",
    "colors.tabs.highlighted.text",
    "colors.tabs.highlighted.line.regular",
    "colors.tabs.selected.text",
    "colors.tabs.selected.line.regular",
    "colors.messages.backgrounds.regular",
    "colors.messages.backgrounds.alternate",
    "colors.messages.disabled",
    "colors.messages.highlightAnimationEnd",
    "colors.messages.highlightAnimationStart",
    "colors.messages.selection",
    "colors.messages.textColors.caret",
    "colors.messages.textColors.chatPlaceholder",
    "colors.messages.textColors.link",
    "colors.messages.textColors.regular",
    "colors.messages.textColors.system",
    "colors.overlayMessages.backgrounds.alternate",
    "colors.overlayMessages.backgrounds.regular",
    "colors.overlayMessages.disabled",
    "colors.overlayMessages.selection",
    "colors.overlayMessages.textColors.caret",
    "colors.overlayMessages.textColors.regular",
    "colors.overlayMessages.textColors.system",
    "colors.overlayMessages.background",
    "colors.scrollbars.background",
    "colors.scrollbars.thumb",
    "colors.scrollbars.thumbSelected",
    "colors.splits.background",
    "colors.splits.dropPreview",
    "colors.splits.dropPreviewBorder",
    "colors.splits.dropTargetRect",
    "colors.splits.dropTargetRectBorder",
    "colors.splits.resizeHandle",
    "colors.splits.resizeHandleBackground",
    "colors.splits.messageSeperator",
    "colors.splits.header.background",
    "colors.splits.header.border",
    "colors.splits.header.focusedBackground",
    "colors.splits.header.focusedBorder",
    "colors.splits.header.text",
    "colors.splits.header.focusedText",
    "colors.splits.input.background",
    "colors.splits.input.backgroundPulse",
    "colors.splits.input.searchFailText",
    "colors.splits.input.searchHighlightBackground",
    "colors.splits.input.text",
};

QJsonValue digValue(const QJsonObject &root, const QString &path)
{
    QJsonObject current = root;
    const QStringList parts = path.split('.');
    for (qsizetype i = 0; i < parts.size() - 1; ++i)
    {
        current = current.value(parts[i]).toObject();
    }
    return current.value(parts.last());
}

}  // namespace

TEST(LimerinoTheme, EveryRequiredLeafPresent)
{
    const auto dark = generateTheme(LimerinoThemeSeed::darkPreset());
    for (const char *leaf : REQUIRED_LEAVES)
    {
        const auto val = digValue(dark, QString::fromLatin1(leaf));
        EXPECT_TRUE(val.isString()) << "missing or non-string leaf: " << leaf;
        EXPECT_FALSE(val.toString().isEmpty()) << "empty leaf: " << leaf;
    }
}

TEST(LimerinoTheme, StyleSheetNeverEmitted)
{
    const auto dark = generateTheme(LimerinoThemeSeed::darkPreset());
    const auto splitsInput =
        dark["colors"].toObject()["splits"].toObject()["input"].toObject();
    EXPECT_FALSE(splitsInput.contains("styleSheet"))
        << "splits.input.styleSheet must not be emitted";
    EXPECT_FALSE(splitsInput.contains("stylesheets"));
}

TEST(LimerinoTheme, SchemaUsesPublicLimerinoUrl)
{
    const auto dark = generateTheme(LimerinoThemeSeed::darkPreset());
    EXPECT_EQ(
        dark[QStringLiteral("$schema")].toString(),
        QStringLiteral("https://raw.githubusercontent.com/lagx/Limerino/"
                       "limerino/docs/ChatterinoTheme.schema.json"));
}

TEST(LimerinoTheme, IconThemeFollowsLightness)
{
    const auto dark = generateTheme(LimerinoThemeSeed::darkPreset());
    const auto light = generateTheme(LimerinoThemeSeed::lightPreset());
    // iconTheme is the inverse of the visual theme (dark theme -> light icons)
    EXPECT_EQ(dark["metadata"].toObject()["iconTheme"].toString(),
              QStringLiteral("light"));
    EXPECT_EQ(light["metadata"].toObject()["iconTheme"].toString(),
              QStringLiteral("dark"));
}

TEST(LimerinoTheme, Deterministic)
{
    const auto seed = LimerinoThemeSeed::darkPreset();
    const auto a = QJsonDocument(generateTheme(seed)).toJson();
    const auto b = QJsonDocument(generateTheme(seed)).toJson();
    EXPECT_EQ(a, b);
}

TEST(LimerinoTheme, LowContrastSeedIsFlagged)
{
    // background == text reads as invisible; the warnings list must fire.
    LimerinoThemeSeed bad = LimerinoThemeSeed::darkPreset();
    bad.text = bad.background;  // contrast 1.0
    const auto warnings = contrastWarnings(bad);
    EXPECT_FALSE(warnings.isEmpty());
    EXPECT_TRUE(warnings.join(' ').contains("primary text"));
}

TEST(LimerinoTheme, TransparentSurvivesSerialization)
{
    const auto theme = generateTheme(LimerinoThemeSeed::lightPreset());
    const auto overlay =
        theme["colors"].toObject()["overlayMessages"].toObject();
    const auto regular =
        overlay["backgrounds"].toObject()["regular"].toString();
    // The literal string survives; the parse side converts it.
    EXPECT_EQ(regular, QStringLiteral("transparent"));
}

TEST(LimerinoTheme, RecolorKeepsUnrelatedLeaves)
{
    QJsonObject backgrounds;
    backgrounds.insert(QStringLiteral("regular"), QStringLiteral("#191919"));
    backgrounds.insert(QStringLiteral("alternate"), QStringLiteral("#222222"));

    QJsonObject textColors;
    textColors.insert(QStringLiteral("regular"), QStringLiteral("#ffffff"));
    textColors.insert(QStringLiteral("link"), QStringLiteral("#4286f4"));

    QJsonObject messages;
    messages.insert(QStringLiteral("backgrounds"), backgrounds);
    messages.insert(QStringLiteral("textColors"), textColors);

    QJsonObject header;
    header.insert(QStringLiteral("background"), QStringLiteral("#2e2e2e"));
    QJsonObject splits;
    splits.insert(QStringLiteral("header"), header);

    QJsonObject colors;
    colors.insert(QStringLiteral("accent"), QStringLiteral("#00aeef"));
    colors.insert(QStringLiteral("messages"), messages);
    colors.insert(QStringLiteral("splits"), splits);

    QJsonObject base;
    base.insert(QStringLiteral("colors"), colors);

    const auto from = seedFromThemeJson(base);
    EXPECT_EQ(from.background.name(QColor::HexRgb).toLower(),
              QStringLiteral("#191919"));
    EXPECT_EQ(from.surface.name(QColor::HexRgb).toLower(),
              QStringLiteral("#2e2e2e"));
    EXPECT_EQ(from.accent.name(QColor::HexRgb).toLower(),
              QStringLiteral("#00aeef"));
    EXPECT_EQ(from.text.name(QColor::HexRgb).toLower(),
              QStringLiteral("#ffffff"));

    LimerinoThemeSeed to = from;
    to.background = QColor(QStringLiteral("#112233"));
    to.accent = QColor(QStringLiteral("#ff00aa"));

    const auto out = recolorTheme(base, from, to);
    const auto outColors = out[QStringLiteral("colors")].toObject();
    const auto outMessages = outColors[QStringLiteral("messages")].toObject();
    const auto bgs = outMessages[QStringLiteral("backgrounds")].toObject();
    const auto texts = outMessages[QStringLiteral("textColors")].toObject();
    EXPECT_EQ(bgs[QStringLiteral("regular")].toString().toLower(),
              QStringLiteral("#112233"));
    EXPECT_EQ(bgs[QStringLiteral("alternate")].toString().toLower(),
              QStringLiteral("#222222"));
    EXPECT_EQ(texts[QStringLiteral("link")].toString().toLower(),
              QStringLiteral("#4286f4"));
    EXPECT_EQ(texts[QStringLiteral("regular")].toString().toLower(),
              QStringLiteral("#ffffff"));
    EXPECT_EQ(outColors[QStringLiteral("accent")].toString().toLower(),
              QStringLiteral("#ff00aa"));
}

TEST(LimerinoTheme, BuiltinDarkAlternateIsNotGuessedBlend)
{
    const auto dark = loadBuiltinThemeJson(BuiltinTheme::Dark);
    if (!dark.has_value())
    {
        GTEST_SKIP() << "built-in Dark.json not in test resources";
    }
    const auto from = seedFromThemeJson(*dark);
    const auto generated = generateThemeFromBase(*dark, from, from);
    const auto expectedAlternate =
        (*dark)[QStringLiteral("colors")]
            .toObject()[QStringLiteral("messages")]
            .toObject()[QStringLiteral("backgrounds")]
            .toObject()[QStringLiteral("alternate")]
            .toString()
            .toLower();
    const auto gotAlternate =
        generated[QStringLiteral("colors")]
            .toObject()[QStringLiteral("messages")]
            .toObject()[QStringLiteral("backgrounds")]
            .toObject()[QStringLiteral("alternate")]
            .toString()
            .toLower();
    EXPECT_EQ(gotAlternate, expectedAlternate);
    EXPECT_EQ(expectedAlternate, QStringLiteral("#222222"));
}
