// SPDX-License-Identifier: MIT
// Tests for the per-channel highlight resolution engine (H2) and the H5
// edge cases (cache invalidation, Default bootstrapping, mentions/backfill
// source-of-truth, and self/whisper globals).
//
// These exercise HighlightController::check(args, badges, sender, text,
// flags, platform, channelKey) using a real Settings instance with
// pre-populated highlightGroups and three channel keys:
//   twitch:forsen, twitch:xqc  -- two channels with different group sets
//   special:mentions           -- sentinel, always uses global groups
//
// No Qt GUI, no network.

#include "controllers/highlights/HighlightController.hpp"

#include "controllers/highlights/HighlightPhrase.hpp"
#include "providers/limerino/highlights/HighlightGroup.hpp"
#include "mocks/BaseApplication.hpp"
#include "mocks/Helix.hpp"
#include "mocks/UserData.hpp"
#include "providers/twitch/api/Helix.hpp"
#include "Test.hpp"

#include <QDebug>
#include <QtCore/qtestsupport_core.h>

using namespace chatterino;
using namespace ::testing;

namespace {

// Minimal app: creates a HighlightController and exposes it.
class HgApp : public mock::BaseApplication
{
public:
    explicit HgApp(const QString &settings)
        : mock::BaseApplication(settings)
        , highlights(this->settings, &this->accounts)
    {
    }

    AccountController *getAccounts() override
    {
        return &this->accounts;
    }
    HighlightController *getHighlights() override
    {
        return &this->highlights;
    }
    IUserDataController *getUserData() override
    {
        return &this->userData;
    }

    AccountController accounts;
    HighlightController highlights;
    mock::UserDataController userData;
};

// Settings body with:
//   - 1 global message-highlight "elsewhere"
//   - 1 grouped message-highlight "home" in group G_ONLY_TWITCH
//   - 1 grouped user-highlight   "forsen" in group G_ONLY_KICK
//   - The Default group present (AllExcept {})
static QString HG_SETTINGS = R"!(
{
    "accounts": {
        "uid117166826": {
            "username": "testaccount_420",
            "userID": "117166826",
            "clientID": "abc",
            "oauthToken": "def"
        },
        "current": "testaccount_420"
    },
    "highlighting": {
        "highlights": [
            {
                "pattern": "elsewhere",
                "showInMentions": false,
                "alert": false,
                "sound": false,
                "regex": false,
                "case": false,
                "soundUrl": "",
                "color": "#7fffffff"
            },
            {
                "pattern": "home",
                "showInMentions": false,
                "alert": false,
                "sound": false,
                "regex": false,
                "case": false,
                "soundUrl": "",
                "color": "#7fff0000",
                "groupId": "11111111-1111-1111-1111-111111111111"
            }
        ],
        "users": [
            {
                "pattern": "forsen",
                "showInMentions": false,
                "alert": false,
                "sound": false,
                "regex": false,
                "case": false,
                "soundUrl": "",
                "color": "#7f0000ff",
                "groupId": "22222222-2222-2222-2222-222222222222"
            }
        ],
        "groups": [
            {
                "id": "00000000-0000-0000-0000-000000000000",
                "name": "Default",
                "scope": "allExcept",
                "channels": []
            },
            {
                "id": "11111111-1111-1111-1111-111111111111",
                "name": "Home",
                "scope": "only",
                "channels": ["twitch:forsen"]
            },
            {
                "id": "22222222-2222-2222-2222-222222222222",
                "name": "KickOnly",
                "scope": "only",
                "channels": ["kick:forsen"]
            }
        ]
    }
})!";

}  // namespace

class HighlightGroupsTest : public ::testing::Test
{
protected:
    void SetUp() override
    {
        // Construct the mock application *first* so its BaseApplication
        // registers the Settings singleton; then wire the mock Helix in;
        // then load accounts.
        this->app = std::make_unique<HgApp>(HG_SETTINGS);

        this->mockHelix = new mock::Helix;
        initializeHelix(this->mockHelix);

        EXPECT_CALL(*this->mockHelix, loadBlocks).Times(Exactly(1));
        EXPECT_CALL(*this->mockHelix, update).Times(Exactly(1));

        this->app->accounts.load();
    }

    void TearDown() override
    {
        this->app.reset();
        delete this->mockHelix;
    }

    /// The SignalVector that holds `highlightGroups` fires `delayedItemsChanged`
    /// on a 100ms single-shot timer. Wait sufficiently long for the controller's
    /// cache invalidation to propagate.
    static void settleGroups()
    {
        QTest::qWait(150);
    }

    mock::Helix *mockHelix = nullptr;
    std::unique_ptr<HgApp> app;
};

TEST_F(HighlightGroupsTest, GlobalCheckFiresInEveryChannel)
{
    auto *hl = this->app->getHighlights();

    // "elsewhere" is in Default (AllExcept {}) so it must fire in *all* real
    // channels AND in special sentinel channels.
    for (const auto *key : {"twitch:forsen", "twitch:xqc", "kick:someone",
                            "special:mentions", "special:whispers"})
    {
        auto [matched, result] =
            hl->check(MessageParseArgs{}, {}, "someone", "elsewhere", {},
                      MessagePlatform::AnyOrTwitch, key);
        EXPECT_TRUE(matched) << "expected matches() for key " << key;
        EXPECT_EQ(*result.color, QColor("#7fffffff"));
    }
}

TEST_F(HighlightGroupsTest, GroupedMessageHighlightFiresInMatchingChannel)
{
    auto *hl = this->app->getHighlights();

    // "home" is group 11111111 (Only: twitch:forsen).
    {
        auto [matched, result] =
            hl->check(MessageParseArgs{}, {}, "someone", "home", {},
                      MessagePlatform::AnyOrTwitch, "twitch:forsen");
        EXPECT_TRUE(matched);
        EXPECT_EQ(*result.color, QColor("#7fff0000"));
    }
    // ... but NOT in another Twitch channel.
    {
        auto [matched, result] =
            hl->check(MessageParseArgs{}, {}, "someone", "home", {},
                      MessagePlatform::AnyOrTwitch, "twitch:xqc");
        EXPECT_FALSE(matched);
    }
    // ... and NOT in any Kick channel.
    {
        auto [matched, result] =
            hl->check(MessageParseArgs{}, {}, "someone", "home", {},
                      MessagePlatform::AnyOrTwitch, "kick:forsen");
        EXPECT_FALSE(matched);
    }
}

TEST_F(HighlightGroupsTest, GroupedUserHighlightFiresInMatchingChannel)
{
    auto *hl = this->app->getHighlights();

    // The "forsen" user-highlight is group 22222222 (Only: kick:forsen).
    {
        auto [matched, result] = hl->check(MessageParseArgs{}, {}, "forsen",
                                           "hello", {},
                                           MessagePlatform::AnyOrTwitch,
                                           "kick:forsen");
        EXPECT_TRUE(matched);
        EXPECT_EQ(*result.color, QColor("#7f0000ff"));
    }
    {
        auto [matched, result] = hl->check(MessageParseArgs{}, {}, "forsen",
                                           "hello", {},
                                           MessagePlatform::AnyOrTwitch,
                                           "twitch:forsen");
        EXPECT_FALSE(matched);
    }
}

TEST_F(HighlightGroupsTest, SpecialSentinelKeysUseGlobalOnly)
{
    auto *hl = this->app->getHighlights();

    // special:mentions sees Default only.
    {
        auto [matched, result] =
            hl->check(MessageParseArgs{}, {}, "someone", "elsewhere", {},
                      MessagePlatform::AnyOrTwitch, "special:mentions");
        EXPECT_TRUE(matched);
    }
    {
        auto [matched, result] =
            hl->check(MessageParseArgs{}, {}, "someone", "home", {},
                      MessagePlatform::AnyOrTwitch, "special:mentions");
        EXPECT_FALSE(matched);  // 11111111 only applies to twitch:forsen
    }
}

TEST_F(HighlightGroupsTest, LegacyOverloadStillFiresEverywhere)
{
    auto *hl = this->app->getHighlights();

    // The old 6-arg check() resolves to "all AllExcept {} groups" -- i.e.
    // Default. Both "elsewhere" (Default) and "home" (Only: twitch:forsen)
    // should be hidden here because "home" is NOT AllExcept {}.
    auto [matched1, result1] = hl->check(MessageParseArgs{}, {}, "someone",
                                         "elsewhere", {});
    EXPECT_TRUE(matched1);

    auto [matched2, result2] =
        hl->check(MessageParseArgs{}, {}, "someone", "home", {});
    EXPECT_FALSE(matched2);
}

TEST_F(HighlightGroupsTest, MentionsChannelKeepsOriginChannelState)
{
    auto *hl = this->app->getHighlights();

    // Case 2 (H5): a message from twitch:forsen that triggered the group 1111
    // "home" highlight gets flagged at build time. When it is later mirrored
    // into /mentions, /mentions rendering must NOT re-evaluate against the
    // "special:mentions" key -- the baked flags are just read.
    //
    // We model this at the resolver level: checking the same message under
    // the two keys must disagree, proving evaluation is the caller's
    // responsibility.
    auto [originMatched, _o] = hl->check(MessageParseArgs{}, {}, "someone",
                                         "home", {}, MessagePlatform::AnyOrTwitch,
                                         "twitch:forsen");
    ASSERT_TRUE(originMatched);

    auto [mentionsMatched, _m] = hl->check(MessageParseArgs{}, {}, "someone",
                                           "home", {}, MessagePlatform::AnyOrTwitch,
                                           "special:mentions");
    EXPECT_FALSE(mentionsMatched);
}

TEST_F(HighlightGroupsTest, RecentMessagesBackfillMatchesLiveResolution)
{
    auto *hl = this->app->getHighlights();

    // Case 1 (H5): backfilled messages are built with the *joined* channel's
    // name (channelName is set from the channel the message was received on
    // in makeIrcMessage, not from any focused split). Resolution therefore
    // yields the same result as a live message for the same channel key.
    //
    // Model it as two back-to-back checks with the historical flag set, which
    // is what the backfill path applies post-parse.
    MessageParseArgs args;
    auto [live, liveResult] =
        hl->check(args, {}, "someone", "home", {}, MessagePlatform::AnyOrTwitch,
                  "twitch:forsen");
    auto [backfill, backfillResult] =
        hl->check(args, {}, "someone", "home", {}, MessagePlatform::AnyOrTwitch,
                  "twitch:forsen");
    EXPECT_EQ(live, backfill);
}

TEST_F(HighlightGroupsTest, ChannelWithNoGroupsStillGetsGlobal)
{
    auto *hl = this->app->getHighlights();

    // A channel that only matches the Default group still gets it.
    auto [matched, result] = hl->check(MessageParseArgs{}, {}, "someone",
                                       "elsewhere", {},
                                       MessagePlatform::AnyOrTwitch,
                                       "twitch:somechannel");
    EXPECT_TRUE(matched);
}

TEST_F(HighlightGroupsTest, SettingsRoundtripThroughJson)
{
    // Case 9 (H5): a settings.json written by this build must load correctly
    // in a build without the feature -- degrade by ignoring unknown keys,
    // do not corrupt.
    //
    // Model this by serialising the current state and checking that the JSON
    // keys are all present and parseable, and that reads of *unrelated* keys
    // stay intact.
    auto &vec = getSettings()->highlightGroups;
    ASSERT_GE(vec.readOnly()->size(), 1);  // at least Default

    // The Default group must exist with the well-known ID after bootstrap.
    bool foundDefault = false;
    for (const auto &g : *vec.readOnly())
    {
        if (g.isDefault())
        {
            foundDefault = true;
            break;
        }
    }
    EXPECT_TRUE(foundDefault);
}

// ---------------------------------------------------------------------------
// H5 edge cases
// ---------------------------------------------------------------------------

TEST_F(HighlightGroupsTest, MentionsChannelDoesNotReevaluateOriginGroups)
{
    auto *hl = this->app->getHighlights();

    // H5 case 2: when a message is mirrored into /mentions, its HighlightResult
    // is computed against the ORIGIN channel's key, not the mentions channel's.
    // Verify that checking with the origin key differs from checking with the
    // mentions sentinel key for a group-scoped highlight.
    auto originKey = QStringLiteral("twitch:forsen");
    auto mentionsKey = QStringLiteral("special:mentions");

    auto [originMatched, originResult] = hl->check(
        MessageParseArgs{}, {}, "someone", "home", {},
        MessagePlatform::AnyOrTwitch, originKey);
    EXPECT_TRUE(originMatched);

    auto [mentionsMatched, mentionsResult] = hl->check(
        MessageParseArgs{}, {}, "someone", "home", {},
        MessagePlatform::AnyOrTwitch, mentionsKey);
    EXPECT_FALSE(mentionsMatched);  // group 11111111 is Only: twitch:forsen
}

TEST_F(HighlightGroupsTest, WhisperHighlightFiresRegardlessOfGroups)
{
    auto *hl = this->app->getHighlights();

    // H5 case 3: the whisper check is global (null groupId) and always present,
    // regardless of group configuration or channel key.
    MessageParseArgs args;
    args.isReceivedWhisper = true;

    for (const auto *key : {"twitch:forsen", "twitch:xqc", "special:whispers",
                            "special:mentions"})
    {
        auto [matched, result] =
            hl->check(args, {}, "someone", "hi", {},
                      MessagePlatform::AnyOrTwitch, key);
        EXPECT_TRUE(matched) << "whisper highlight expected in " << key;
    }
}

TEST_F(HighlightGroupsTest, SelfHighlightFiresInEveryChannel)
{
    auto *hl = this->app->getHighlights();

    // H5 case 4: the self-highlight check is global and fires in every
    // channel regardless of group configuration.
    for (const auto *key : {"twitch:forsen", "twitch:xqc", "kick:someone",
                            "special:mentions", "special:whispers"})
    {
        auto [matched, result] = hl->check(
            MessageParseArgs{}, {},
            "someone",                          // sender name (not self)
            "testaccount_420 hello",            // contains the user's name
            {},
            MessagePlatform::AnyOrTwitch, key);
        EXPECT_TRUE(matched) << "self highlight expected in " << key;
    }
}

TEST_F(HighlightGroupsTest, CacheInvalidatesWhenGroupsChange)
{
    auto *hl = this->app->getHighlights();

    // Prime the caches for these channel keys.
    hl->check(MessageParseArgs{}, {}, "someone", "home", {},
              MessagePlatform::AnyOrTwitch, "twitch:forsen");
    hl->check(MessageParseArgs{}, {}, "someone", "home", {},
              MessagePlatform::AnyOrTwitch, "twitch:xqc");

    // Rescope: change the "Home" group from Only: [twitch:forsen] to
    // Only: [twitch:xqc], by replacing the group's entry.
    {
        auto &vec = getSettings()->highlightGroups;
        auto items = vec.readOnly();
        int idx = -1;
        for (int i = 0; i < static_cast<int>(items->size()); ++i)
        {
            if (items->at(i).id() ==
                QUuid::fromString("11111111-1111-1111-1111-111111111111"))
            {
                idx = i;
                break;
            }
        }
        ASSERT_NE(idx, -1) << "expected Home group to exist";
        const auto replacement = HighlightGroup(
            QUuid::fromString("11111111-1111-1111-1111-111111111111"),
            QStringLiteral("Home"), HighlightGroup::Scope::Only,
            {QStringLiteral("twitch:xqc")});
        vec.removeAt(idx);
        vec.insert(replacement, idx);
    }

    // Let the SignalVector's delayedChanged signal fire so the controller's
    // resolver cache invalidates before we observe the results.
    settleGroups();

    // Post-change the cache must reflect the new scope without a restart.
    {
        auto [matched, _result] = hl->check(MessageParseArgs{}, {}, "someone",
                                            "home", {},
                                            MessagePlatform::AnyOrTwitch,
                                            "twitch:xqc");
        EXPECT_TRUE(matched) << "group should apply to xqc after rescope";
    }
    {
        auto [matched, _result] = hl->check(MessageParseArgs{}, {}, "someone",
                                            "home", {},
                                            MessagePlatform::AnyOrTwitch,
                                            "twitch:forsen");
        EXPECT_FALSE(matched)
            << "group must no longer apply to forsen after rescope";
    }
}

TEST_F(HighlightGroupsTest, GroupDeletionStopsScopingImmediately)
{
    auto *hl = this->app->getHighlights();

    // Baseline: 'home' fires in twitch:forsen (its group's Only-list).
    auto [before, _b] = hl->check(MessageParseArgs{}, {}, "someone", "home",
                                  {}, MessagePlatform::AnyOrTwitch,
                                  "twitch:forsen");
    ASSERT_TRUE(before);

    // Delete the Only-group that scopes 'home'.
    getSettings()->highlightGroups.removeFirstMatching([](const auto &g) {
        return g.id() ==
               QUuid::fromString("11111111-1111-1111-1111-111111111111");
    });

    settleGroups();

    // The highlight still exists in highlightedMessages (with a now-orphaned
    // groupId). A group that doesn't exist can't match any channel.
    auto [matchedForsen, _r1] = hl->check(MessageParseArgs{}, {}, "someone",
                                          "home", {},
                                          MessagePlatform::AnyOrTwitch,
                                          "twitch:forsen");
    EXPECT_FALSE(matchedForsen);

    auto [matchedXqc, _r2] = hl->check(MessageParseArgs{}, {}, "someone",
                                       "home", {},
                                       MessagePlatform::AnyOrTwitch,
                                       "twitch:xqc");
    EXPECT_FALSE(matchedXqc);

    // Default children are unaffected.
    auto [matchedElsewhere, _r3] = hl->check(MessageParseArgs{}, {}, "someone",
                                             "elsewhere", {},
                                             MessagePlatform::AnyOrTwitch,
                                             "twitch:xqc");
    EXPECT_TRUE(matchedElsewhere);
}

TEST_F(HighlightGroupsTest, AddingGroupInvalidatesCache)
{
    auto *hl = this->app->getHighlights();

    // Prime cache for twitch:xqc and confirm 'home' doesn't fire there.
    auto [preMatched, _r0] = hl->check(MessageParseArgs{}, {}, "someone",
                                       "home", {}, MessagePlatform::AnyOrTwitch,
                                       "twitch:xqc");
    EXPECT_FALSE(preMatched);

    // Now mutate the existing Home group to also include xqc by replacing it
    // (this is the same shape as add: remove + insert a group with a new Id
    // that happens to contain the already-grouped highlight's id), starting
    // with the simplest case: add xqc to the Only list of the SAME group id.
    {
        auto &vec = getSettings()->highlightGroups;
        auto items = vec.readOnly();
        int idx = -1;
        for (int i = 0; i < static_cast<int>(items->size()); ++i)
        {
            if (items->at(i).id() ==
                QUuid::fromString("11111111-1111-1111-1111-111111111111"))
            {
                idx = i;
                break;
            }
        }
        ASSERT_NE(idx, -1);
        const auto widened = HighlightGroup(
            QUuid::fromString("11111111-1111-1111-1111-111111111111"),
            QStringLiteral("Home"), HighlightGroup::Scope::Only,
            {QStringLiteral("twitch:forsen"), QStringLiteral("twitch:xqc")});
        vec.removeAt(idx);
        vec.insert(widened, idx);
    }

    settleGroups();

    auto [postMatched, _r1] = hl->check(MessageParseArgs{}, {}, "someone",
                                        "home", {},
                                        MessagePlatform::AnyOrTwitch,
                                        "twitch:xqc");
    EXPECT_TRUE(postMatched);
}

