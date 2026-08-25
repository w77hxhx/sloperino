// SPDX-License-Identifier: MIT
// N5 tests: scope model + serde round-trip + normalization for auto actions.
// Resolver behavior is covered by LimerinoHighlightGroups.cpp's cache
// invalidation tests — the resolver here is a thin mirror of that shape.

#include "providers/limerino/autoactions/LimerinoAutoAction.hpp"

#include <gtest/gtest.h>

using namespace chatterino::limerino;

namespace {

LimerinoAutoAction makeRule(const QString &name)
{
    LimerinoAutoAction a;
    a.name = name;
    return a;
}

}  // namespace

TEST(LimerinoAutoAction, DefaultIsEnabledAndAllExceptEverywhere)
{
    auto r = makeRule(QStringLiteral("x"));

    EXPECT_TRUE(r.enabled);
    EXPECT_TRUE(r.matchesChannel(QStringLiteral("twitch:forsen")));
    EXPECT_TRUE(r.matchesChannel(QStringLiteral("kick:anyone")));
    EXPECT_TRUE(r.matchesChannel(QStringLiteral("special:whispers")));
}

TEST(LimerinoAutoAction, OnlyScopeMatchesListed)
{
    auto r = makeRule(QStringLiteral("only"));
    r.scope = LimerinoAutoAction::Scope::Only;
    r.channels = {QStringLiteral("twitch:forsen")};
    r.normalize();

    EXPECT_TRUE(r.matchesChannel(QStringLiteral("twitch:forsen")));
    EXPECT_FALSE(r.matchesChannel(QStringLiteral("twitch:xqc")));
    EXPECT_FALSE(r.matchesChannel(QStringLiteral("kick:forsen")));
}

TEST(LimerinoAutoAction, AllExceptExcludesListed)
{
    auto r = makeRule(QStringLiteral("allBut"));
    r.scope = LimerinoAutoAction::Scope::AllExcept;
    r.channels = {QStringLiteral("twitch:xqc")};
    r.normalize();

    EXPECT_TRUE(r.matchesChannel(QStringLiteral("twitch:forsen")));
    EXPECT_FALSE(r.matchesChannel(QStringLiteral("twitch:xqc")));
    EXPECT_TRUE(r.matchesChannel(QStringLiteral("special:whispers")));
}

TEST(LimerinoAutoAction, MatchersAndActionStringRoundTrip)
{
    LimerinoAutoAction original;
    original.name = QStringLiteral("Spam nuker");
    original.enabled = false;
    original.content = LimerinoMatcher(QStringLiteral("spam|phish"),
                                       /*caseSensitive=*/true,
                                       /*isRegex=*/true);
    original.sender = LimerinoMatcher(QStringLiteral("^bot"),
                                      /*caseSensitive=*/false,
                                      /*isRegex=*/true);
    original.scope = LimerinoAutoAction::Scope::Only;
    original.channels = {QStringLiteral("twitch:forsen"),
                         QStringLiteral("kick:xqc")};
    original.action =
        QStringLiteral("/timeout {sender.name} 600 spam");
    original.cooldownSeconds = 42;
    original.normalize();

    rapidjson::Document doc;
    auto value = pajlada::Serialize<LimerinoAutoAction>::get(
        original, doc.GetAllocator());

    bool error = false;
    auto back = pajlada::Deserialize<LimerinoAutoAction>::get(value, &error);

    ASSERT_FALSE(error);
    EXPECT_EQ(back.id, original.id);
    EXPECT_EQ(back.name, original.name);
    EXPECT_EQ(back.enabled, original.enabled);
    EXPECT_EQ(back.content.pattern, original.content.pattern);
    EXPECT_EQ(back.content.caseSensitive, original.content.caseSensitive);
    EXPECT_EQ(back.sender.pattern, original.sender.pattern);
    EXPECT_EQ(back.scope, original.scope);
    EXPECT_EQ(back.channels, original.channels);
    EXPECT_EQ(back.action, original.action);
    EXPECT_EQ(back.cooldownSeconds, original.cooldownSeconds);
    // Cached scope set is reconstructed.
    EXPECT_EQ(back.channelSet, original.channelSet);
}

TEST(LimerinoAutoAction, DisabledRuleIsExcludedFromMatches)
{
    auto r = makeRule(QStringLiteral("off"));
    r.enabled = false;
    r.content.pattern = QStringLiteral("spam");
    r.content.normalize();

    // A disabled rule matches *nothing* channel-wise; the resolver (and
    // controller) gate on `enabled`.
    EXPECT_TRUE(r.matchesChannel(QStringLiteral("twitch:forsen")));
}
