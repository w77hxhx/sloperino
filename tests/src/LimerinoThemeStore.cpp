// SPDX-License-Identifier: MIT

#include "providers/limerino/theme/LimerinoThemeStore.hpp"

#include <gtest/gtest.h>

using namespace chatterino::limerino;

namespace {

LimerinoThemeSeed sampleSeed()
{
    LimerinoThemeSeed s;
    s.background = QColor(QStringLiteral("#191919"));
    s.surface = QColor(QStringLiteral("#2e2e2e"));
    s.accent = QColor(QStringLiteral("#00aeef"));
    s.text = QColor(QStringLiteral("#ffffff"));
    return s;
}

}  // namespace

TEST(LimerinoThemeStore, SeedRoundTrip)
{
    const auto json = serializeSeedFile(QStringLiteral("My theme"), sampleSeed());
    ASSERT_TRUE(isSeedFile(json));
    const auto parsed = parseSeedFile(json, nullptr);
    ASSERT_TRUE(parsed.has_value());
    EXPECT_EQ(parsed->name, QStringLiteral("My theme"));
    EXPECT_EQ(parsed->seed.background, QColor(QStringLiteral("#191919")));
    EXPECT_EQ(parsed->seed.surface, QColor(QStringLiteral("#2e2e2e")));
    EXPECT_EQ(parsed->seed.accent, QColor(QStringLiteral("#00aeef")));
    EXPECT_EQ(parsed->seed.text, QColor(QStringLiteral("#ffffff")));
}

TEST(LimerinoThemeStore, VersionGateRejectsOtherVersions)
{
    auto json = serializeSeedFile(QStringLiteral("x"), sampleSeed());
    json.insert(QStringLiteral("limerinoThemeVersion"), 2);
    QString err;
    const auto parsed = parseSeedFile(json, &err);
    EXPECT_FALSE(parsed.has_value());
}

TEST(LimerinoThemeStore, FullThemeIsNotASeedFile)
{
    // A full themes.json produced by generateTheme must NOT look like a seed;
    // the UI refuses "reduce it to 4 colors" imports.
    QJsonObject fullTheme{
        {QStringLiteral("$schema"), QStringLiteral("...")},
        {QStringLiteral("metadata"),
         QJsonObject{{QStringLiteral("iconTheme"), QStringLiteral("light")}}},
        {QStringLiteral("colors"),
         QJsonObject{{QStringLiteral("accent"), QStringLiteral("#00aeef")}}},
    };
    EXPECT_FALSE(isSeedFile(fullTheme));
    QString err;
    EXPECT_FALSE(parseSeedFile(fullTheme, &err).has_value());
}

TEST(LimerinoThemeStore, FilenameSanitizer)
{
    EXPECT_EQ(sanitizeThemeFilename(QStringLiteral("My theme")),
              QStringLiteral("My theme.json"));
    // Windows-invalid characters are stripped.
    EXPECT_EQ(sanitizeThemeFilename(QStringLiteral("a/b\\c:d")),
              QStringLiteral("abcd.json"));
    // Empty names never produce an empty filename.
    EXPECT_EQ(sanitizeThemeFilename(QStringLiteral("  ")),
              QStringLiteral("Limerino theme.json"));
    // A .json suffix is appended exactly once.
    EXPECT_EQ(sanitizeThemeFilename(QStringLiteral("foo.json")),
              QStringLiteral("foo.json"));
}
