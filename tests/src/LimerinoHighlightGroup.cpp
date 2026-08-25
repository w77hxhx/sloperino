// SPDX-License-Identifier: MIT
// Unit tests for the Limerino typed per-channel highlight data model.
// These are value-type tests (no Qt GUI needed); they exercise the
// non-serde path. The pajlada-based serialisation is exercised
// indirectly by the real-settings load in HighlightController tests.

#include "providers/limerino/highlights/HighlightGroup.hpp"

#include <gtest/gtest.h>
#include <rapidjson/document.h>

using namespace chatterino;

// ---------------------------------------------------------------------------
// Basic construction & normalisation
// ---------------------------------------------------------------------------

TEST(LimerinoHighlightGroup, Normalisation)
{
    HighlightGroup g(QUuid::createUuid(), "test",
                     HighlightGroup::Scope::AllExcept,
                     {"  Twitch:FORSEN  ", "twitch:forsen", "", "xqc", "xqc"});

    // Trimmed, lowercased, dupes removed, empties dropped.
    ASSERT_EQ(g.channels().size(), 4);
    EXPECT_TRUE(g.channels().contains("twitch:forsen"));
    EXPECT_TRUE(g.channels().contains("xqc"));
    EXPECT_TRUE(g.channels().contains("  Twitch:FORSEN  ") == false);
}

TEST(LimerinoHighlightGroup, Equality)
{
    auto id = QUuid::createUuid();
    HighlightGroup a(id, "Group A", HighlightGroup::Scope::AllExcept,
                     {"twitch:forsen"});
    HighlightGroup b(id, "Group A", HighlightGroup::Scope::AllExcept,
                     {"twitch:forsen"});
    HighlightGroup c(QUuid::createUuid(), "Group A",
                     HighlightGroup::Scope::AllExcept, {"twitch:forsen"});

    EXPECT_TRUE(a == b);
    EXPECT_FALSE(a == c);  // different id
}

// ---------------------------------------------------------------------------
// matches() -- the core scope check
// ---------------------------------------------------------------------------

TEST(LimerinoHighlightGroup, MatchesAllExceptEmpty)
{
    HighlightGroup g(QUuid::createUuid(), "Everywhere",
                     HighlightGroup::Scope::AllExcept, {});

    // Any channel key matches when the exclusion list is empty.
    EXPECT_TRUE(g.matches("twitch:forsen"));
    EXPECT_TRUE(g.matches("kick:someone"));
    EXPECT_TRUE(g.matches("special:mentions"));
    EXPECT_TRUE(g.matches("special:whispers"));
    EXPECT_TRUE(g.matches("special:live"));
}

TEST(LimerinoHighlightGroup, MatchesAllExceptSpecific)
{
    HighlightGroup g(QUuid::createUuid(), "NotForsen",
                     HighlightGroup::Scope::AllExcept,
                     {"twitch:forsen", "kick:forsen"});

    EXPECT_FALSE(g.matches("twitch:forsen"));
    EXPECT_FALSE(g.matches("kick:forsen"));
    EXPECT_TRUE(g.matches("twitch:xqc"));
    EXPECT_TRUE(g.matches("kick:someone"));
    // Sentinel channels are not listed, so they are NOT excluded.
    EXPECT_TRUE(g.matches("special:mentions"));
    EXPECT_TRUE(g.matches("special:whispers"));
    // Case-insensitive: construction lowercases stored channels,
    // matches() lowercases the lookup key, so equivalent keys match.
    EXPECT_FALSE(g.matches("Twitch:FORSEN"));
}

TEST(LimerinoHighlightGroup, MatchesOnly)
{
    HighlightGroup g(QUuid::createUuid(), "OnlyForsen",
                     HighlightGroup::Scope::Only,
                     {"twitch:forsen", "kick:forsen"});

    EXPECT_TRUE(g.matches("twitch:forsen"));
    EXPECT_TRUE(g.matches("kick:forsen"));
    EXPECT_FALSE(g.matches("twitch:xqc"));
    EXPECT_FALSE(g.matches("kick:someone"));
    // Sentinel channels never match a specific Only list unless listed.
    EXPECT_FALSE(g.matches("special:mentions"));
    EXPECT_FALSE(g.matches("special:whispers"));
}

// ---------------------------------------------------------------------------
// displayName() -- unnamed groups show a scope summary
// ---------------------------------------------------------------------------

TEST(LimerinoHighlightGroup, DisplayName)
{
    HighlightGroup named(QUuid::createUuid(), "My Group",
                         HighlightGroup::Scope::Only, {"a"});
    EXPECT_EQ(named.displayName(), "My Group");

    HighlightGroup unnamedEverywhere(QUuid::createUuid(), "",
                                     HighlightGroup::Scope::AllExcept, {});
    EXPECT_EQ(unnamedEverywhere.displayName(), "Everywhere");

    HighlightGroup unnamedAllExcept(QUuid::createUuid(), "",
                                    HighlightGroup::Scope::AllExcept,
                                    {"twitch:forsen", "kick:someone"});
    EXPECT_EQ(unnamedAllExcept.displayName(),
              "All except: twitch:forsen, kick:someone");

    HighlightGroup unnamedOnly(QUuid::createUuid(), "",
                               HighlightGroup::Scope::Only,
                               {"twitch:forsen"});
    EXPECT_EQ(unnamedOnly.displayName(), "Only: twitch:forsen");
}

// ---------------------------------------------------------------------------
// Default group semantics
// ---------------------------------------------------------------------------

TEST(LimerinoHighlightGroup, DefaultBootstrap)
{
    // A group constructed with DEFAULT_ID is The Default / global group.
    HighlightGroup g(HighlightGroup::DEFAULT_ID, "Default",
                     HighlightGroup::Scope::AllExcept, {});

    EXPECT_TRUE(g.isDefault());
    EXPECT_TRUE(g.matches("twitch:anything"));
    EXPECT_TRUE(g.matches("special:mentions"));
    EXPECT_TRUE(g.matches("kick:anything"));
}

// ---------------------------------------------------------------------------
// to/from JSON round-trip via the public Serialize/Deserialize API
// ---------------------------------------------------------------------------

TEST(LimerinoHighlightGroup, SerdeRoundtrip)
{
    auto original = HighlightGroup(QUuid::createUuid(), "Test Group",
                                   HighlightGroup::Scope::Only,
                                   {"twitch:forsen", "kick:someone"});

    // Source: a settings-tree-like rapidjson::Value.
    rapidjson::Document d;
    auto &alloc = d.GetAllocator();

    auto json = pajlada::Serialize<HighlightGroup>::get(original, alloc);
    ASSERT_TRUE(json.IsObject());

    auto roundTripped = pajlada::Deserialize<HighlightGroup>::get(json);

    EXPECT_EQ(original, roundTripped);
    EXPECT_EQ(original.id(), roundTripped.id());
    EXPECT_EQ(original.name(), roundTripped.name());
    EXPECT_EQ(original.scope(), roundTripped.scope());
    EXPECT_EQ(original.channels(), roundTripped.channels());
}

TEST(LimerinoHighlightGroup, SerdeAbsentIdFallsBackToDefault)
{
    rapidjson::Document doc;
    doc.Parse(R"({
        "name": "Legacy",
        "scope": "only",
        "channels": ["twitch:forsen"]
    })");
    ASSERT_TRUE(doc.IsObject());
    ASSERT_FALSE(doc.HasParseError());

    auto group = pajlada::Deserialize<HighlightGroup>::get(doc);

    // QUuid::fromString("") is a null QUuid; should land on Default.
    EXPECT_EQ(group.id(), HighlightGroup::DEFAULT_ID);
    EXPECT_TRUE(group.isDefault());
    // Scope and channels should survive migration.
    EXPECT_EQ(group.scope(), HighlightGroup::Scope::Only);
    EXPECT_TRUE(group.channels().contains("twitch:forsen"));
}

TEST(LimerinoHighlightGroup, SerdeMalformedIdFallsBackToDefault)
{
    rapidjson::Document doc;
    doc.Parse(R"({
        "id": "not-a-uuid",
        "name": "Broken",
        "scope": "allExcept",
        "channels": []
    })");
    ASSERT_TRUE(doc.IsObject());
    ASSERT_FALSE(doc.HasParseError());

    auto group = pajlada::Deserialize<HighlightGroup>::get(doc);

    EXPECT_EQ(group.id(), HighlightGroup::DEFAULT_ID);
    EXPECT_TRUE(group.isDefault());
}

TEST(LimerinoHighlightGroup, SerdeScopeDefaultWhenMissing)
{
    rapidjson::Document doc;
    doc.Parse(R"({
        "id": "5db1ba01-a40e-4b79-a35e-6d609465fcab",
        "name": "",
        "channels": []
    })");
    ASSERT_TRUE(doc.IsObject());
    ASSERT_FALSE(doc.HasParseError());

    auto group = pajlada::Deserialize<HighlightGroup>::get(doc);

    // Missing "scope" defaults to AllExcept.
    EXPECT_EQ(group.scope(), HighlightGroup::Scope::AllExcept);
    EXPECT_TRUE(group.matches("twitch:forsen"));
}

// ---------------------------------------------------------------------------
// Sentinel keys behave correctly under both scopes
// ---------------------------------------------------------------------------

TEST(LimerinoHighlightGroup, SentinelChannelBehaviour)
{
    // A Default-scoped group applies in special channels because they are
    // not listed in its AllExcept exclusion list.
    HighlightGroup everywhere(HighlightGroup::DEFAULT_ID, "Default",
                              HighlightGroup::Scope::AllExcept, {});

    EXPECT_TRUE(everywhere.matches("special:whispers"));
    EXPECT_TRUE(everywhere.matches("special:mentions"));
    EXPECT_TRUE(everywhere.matches("special:live"));
    EXPECT_TRUE(everywhere.matches("special:automod"));
    EXPECT_TRUE(everywhere.matches("special:other:somechannel"));

    // The same special channels never match an Only group unless listed.
    HighlightGroup onlyForsen(QUuid::createUuid(), "",
                              HighlightGroup::Scope::Only,
                              {"twitch:forsen"});
    EXPECT_FALSE(onlyForsen.matches("special:whispers"));
    EXPECT_FALSE(onlyForsen.matches("special:mentions"));
}
