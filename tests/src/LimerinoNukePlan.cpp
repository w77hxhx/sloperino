// SPDX-License-Identifier: MIT
// Unit tests for the pure nuke-plan builder (batch N2). No Channel, no app
// state — the engine only sees a vector<MessagePtr>.

#include "providers/limerino/nuke/NukeEngine.hpp"

#include "messages/Message.hpp"
#include "providers/limerino/matcher/LimerinoMatcher.hpp"

#include <gtest/gtest.h>

#include <QDateTime>

using namespace chatterino;
using chatterino::limerino::LimerinoMatcher;
using chatterino::limerino::NukeAction;
using chatterino::limerino::NukePlan;
using chatterino::limerino::buildPlan;
using chatterino::limerino::validateNukeRequest;

namespace {

const QDateTime NOW = QDateTime::fromString(
    QStringLiteral("2026-08-04T12:00:00Z"), Qt::ISODate);

const QString CHANNEL = QStringLiteral("somechannel");
const QString SELF = QString();  // no logged-in self: only the broadcaster is excluded

MessagePtr chatMessage(const QString &login, const QString &text,
                       const QDateTime &time, const QString &id = {})
{
    auto msg = std::make_shared<Message>();
    msg->loginName = login;
    msg->displayName = login;
    msg->messageText = text;
    msg->searchText = text;
    msg->serverReceivedTime = time;
    msg->id = id;
    msg->userID = QStringLiteral("u_%1").arg(login);
    msg->flags = MessageFlags();  // plain chat message
    return msg;
}

std::vector<MessagePtr> snapshot(std::initializer_list<MessagePtr> msgs)
{
    return {msgs.begin(), msgs.end()};
}

}  // namespace

// --- request-level validation --------------------------------------------------

TEST(LimerinoNuke, BothEmptyRejectedByValidator)
{
    LimerinoMatcher empty;
    EXPECT_TRUE(validateNukeRequest(empty, empty, 600).has_value());
    EXPECT_TRUE(validateNukeRequest(LimerinoMatcher{.pattern = "x"}, empty, 0)
                    .has_value());
    EXPECT_TRUE(validateNukeRequest(LimerinoMatcher{.pattern = "x"}, empty, -1)
                    .has_value());
    EXPECT_FALSE(
        validateNukeRequest(LimerinoMatcher{.pattern = "x"}, empty, 600)
            .has_value());
}

// --- snapshot handling ------------------------------------------------------------

TEST(LimerinoNuke, EmptyBufferProducesWarning)
{
    LimerinoMatcher content(QStringLiteral("spam"), false, true);
    LimerinoMatcher sender;

    auto plan = buildPlan({}, MessagePlatform::AnyOrTwitch, CHANNEL, SELF,
                          content, sender, 600, NukeAction::Ban, NOW);

    EXPECT_EQ(plan.messagesScanned, 0);
    EXPECT_EQ(plan.messagesMatched, 0);
    EXPECT_TRUE(plan.targets.isEmpty());
    EXPECT_FALSE(plan.warnings.isEmpty());
}

TEST(LimerinoNuke, LookbackLongerThanBufferIsFlagged)
{
    // Buffer covers the last 60 s; ask for 600 s.
    auto msgs = snapshot(
        {chatMessage(QStringLiteral("user1"), QStringLiteral("hello spam"),
                     NOW.addSecs(-60), QStringLiteral("m1"))});

    LimerinoMatcher content(QStringLiteral("spam"), false, true);
    LimerinoMatcher sender;

    auto plan = buildPlan(msgs, MessagePlatform::AnyOrTwitch, CHANNEL, SELF,
                          content, sender, 600, NukeAction::Ban, NOW);

    EXPECT_TRUE(plan.lookbackExceedsBuffer);
    EXPECT_EQ(plan.bufferOldest, NOW.addSecs(-60));
    EXPECT_EQ(plan.bufferNewest, NOW.addSecs(-60));
}

TEST(LimerinoNuke, LookbackWithinBufferIsNotFlagged)
{
    auto msgs = snapshot(
        {chatMessage(QStringLiteral("user1"), QStringLiteral("spam here"),
                     NOW.addSecs(-10), QStringLiteral("m1"))});

    LimerinoMatcher content(QStringLiteral("spam"), false, true);
    LimerinoMatcher sender;

    auto plan = buildPlan(msgs, MessagePlatform::AnyOrTwitch, CHANNEL, SELF,
                          content, sender, 600, NukeAction::Ban, NOW);

    EXPECT_FALSE(plan.lookbackExceedsBuffer);
}

// --- exclusion of non-chat messages ----------------------------------------------

TEST(LimerinoNuke, DeletedMessagesAreSkipped)
{
    auto m = chatMessage(QStringLiteral("badactor"), QStringLiteral("spam"),
                         NOW.addSecs(-5), QStringLiteral("m1"));
    m->flags.set(MessageFlag::Disabled);  // already deleted

    auto msgs = snapshot({m});

    LimerinoMatcher content(QStringLiteral("spam"), false, true);
    LimerinoMatcher sender;

    auto plan = buildPlan(msgs, MessagePlatform::AnyOrTwitch, CHANNEL, SELF,
                          content, sender, 600, NukeAction::Ban, NOW);

    EXPECT_EQ(plan.messagesMatched, 0);
    EXPECT_TRUE(plan.targets.isEmpty());
}

TEST(LimerinoNuke, SystemAndTimeoutRecordsAreSkipped)
{
    auto sys = chatMessage(QString(), QStringLiteral("spam notice"),
                           NOW.addSecs(-5));
    sys->flags.set(MessageFlag::System);

    auto to = chatMessage(QStringLiteral("badactor"), QStringLiteral("spam"),
                          NOW.addSecs(-5));
    to->flags.set(MessageFlag::Timeout);  // record, not a chat message

    auto msgs = snapshot({sys, to});

    LimerinoMatcher content(QStringLiteral("spam"), false, true);
    LimerinoMatcher sender;

    auto plan = buildPlan(msgs, MessagePlatform::AnyOrTwitch, CHANNEL, SELF,
                          content, sender, 600, NukeAction::Ban, NOW);

    EXPECT_EQ(plan.messagesMatched, 0);
}

// --- self and broadcaster are always excluded ------------------------------------- //

TEST(LimerinoNuke, BroadcasterIsExcluded)
{
    auto msgs = snapshot(
        {chatMessage(CHANNEL, QStringLiteral("spam"), NOW.addSecs(-5)),
         chatMessage(QStringLiteral("badactor"), QStringLiteral("spam"),
                     NOW.addSecs(-5))});

    LimerinoMatcher content(QStringLiteral("spam"), false, true);
    LimerinoMatcher sender;

    auto plan = buildPlan(msgs, MessagePlatform::AnyOrTwitch, CHANNEL, SELF,
                          content, sender, 600, NukeAction::Ban, NOW);

    ASSERT_EQ(plan.targets.size(), 1);
    EXPECT_EQ(plan.targets[0].login, QStringLiteral("badactor"));
}

TEST(LimerinoNuke, SelfIsExcludedWhenProvided)
{
    auto msgs = snapshot(
        {chatMessage(QStringLiteral("operator"), QStringLiteral("spam"),
                     NOW.addSecs(-5)),
         chatMessage(QStringLiteral("badactor"), QStringLiteral("spam"),
                     NOW.addSecs(-5))});

    LimerinoMatcher content(QStringLiteral("spam"), false, true);
    LimerinoMatcher sender;

    auto plan = buildPlan(msgs, MessagePlatform::AnyOrTwitch, CHANNEL,
                          QStringLiteral("operator"), content, sender, 600,
                          NukeAction::Ban, NOW);

    ASSERT_EQ(plan.targets.size(), 1);
    EXPECT_EQ(plan.targets[0].login, QStringLiteral("badactor"));
}

// --- per-action shaping -------------------------------------------------------------

TEST(LimerinoNuke, OneUserManyMessagesProduceOneTarget)
{
    auto msgs = snapshot(
        {chatMessage(QStringLiteral("badactor"), QStringLiteral("spam 1"),
                     NOW.addSecs(-30), QStringLiteral("a")),
         chatMessage(QStringLiteral("badactor"), QStringLiteral("spam 2"),
                     NOW.addSecs(-20), QStringLiteral("b")),
         chatMessage(QStringLiteral("badactor"), QStringLiteral("spam 3"),
                     NOW.addSecs(-10), QStringLiteral("c")),
         chatMessage(QStringLiteral("someoneelse"), QStringLiteral("spam 4"),
                     NOW.addSecs(-5), QStringLiteral("d"))});

    LimerinoMatcher content(QStringLiteral("spam"), false, true);
    LimerinoMatcher sender(QStringLiteral("^badactor$"), false, true);

    auto plan = buildPlan(msgs, MessagePlatform::AnyOrTwitch, CHANNEL, SELF,
                          content, sender, 600, NukeAction::Ban, NOW);

    ASSERT_EQ(plan.targets.size(), 1);
    EXPECT_EQ(plan.targets[0].login, QStringLiteral("badactor"));
    EXPECT_EQ(plan.targets[0].matchedMessages, 3);
    EXPECT_TRUE(plan.messageIds.isEmpty());
}

TEST(LimerinoNuke, DeleteIsPerMessage)
{
    auto msgs = snapshot(
        {chatMessage(QStringLiteral("badactor"), QStringLiteral("spam 1"),
                     NOW.addSecs(-30), QStringLiteral("id-a")),
         chatMessage(QStringLiteral("badactor"), QStringLiteral("spam 2"),
                     NOW.addSecs(-20), QStringLiteral("id-b")),
         chatMessage(QStringLiteral("badactor"), QStringLiteral("spam 3"),
                     NOW.addSecs(-10), QString())});  // no id

    LimerinoMatcher content(QStringLiteral("spam"), false, true);
    LimerinoMatcher sender;

    auto plan = buildPlan(msgs, MessagePlatform::AnyOrTwitch, CHANNEL, SELF,
                          content, sender, 600, NukeAction::Delete, NOW);

    EXPECT_EQ(plan.messageIds.size(), 2);
    EXPECT_TRUE(plan.messageIds.contains(QStringLiteral("id-a")));
    EXPECT_TRUE(plan.messageIds.contains(QStringLiteral("id-b")));
    EXPECT_TRUE(plan.targets.isEmpty());
    // One message lacked an id -> a warning was collected.
    EXPECT_FALSE(plan.warnings.isEmpty());
}

TEST(LimerinoNuke, DeleteAndTimeoutPopulatesBothLists)
{
    auto msgs = snapshot(
        {chatMessage(QStringLiteral("badactor1"), QStringLiteral("spam one"),
                     NOW.addSecs(-30), QStringLiteral("id-a")),
         chatMessage(QStringLiteral("badactor2"), QStringLiteral("spam two"),
                     NOW.addSecs(-20), QStringLiteral("id-b")),
         chatMessage(QStringLiteral("badactor1"), QStringLiteral("spam three"),
                     NOW.addSecs(-10), QStringLiteral("id-c"))});

    LimerinoMatcher content(QStringLiteral("spam"), false, true);
    LimerinoMatcher sender;

    auto plan = buildPlan(msgs, MessagePlatform::AnyOrTwitch, CHANNEL, SELF,
                          content, sender, 600,
                          NukeAction::DeleteAndTimeout, NOW);

    ASSERT_EQ(plan.targets.size(), 2);
    EXPECT_EQ(plan.targets[0].matchedMessages + plan.targets[1].matchedMessages,
              3);
    EXPECT_EQ(plan.messageIds.size(), 3);
}

TEST(LimerinoNuke, InvalidRegexProducesEmptyPlan)
{
    auto msgs = snapshot(
        {chatMessage(QStringLiteral("badactor"), QStringLiteral("spam"),
                     NOW.addSecs(-10))});

    LimerinoMatcher content(QStringLiteral("(unclosed"), false, true);
    ASSERT_TRUE(content.compileError().has_value());
    LimerinoMatcher sender;

    auto plan = buildPlan(msgs, MessagePlatform::AnyOrTwitch, CHANNEL, SELF,
                          content, sender, 600, NukeAction::Ban, NOW);

    EXPECT_EQ(plan.messagesMatched, 0);
    EXPECT_TRUE(plan.targets.isEmpty());
}

TEST(LimerinoNuke, WarnOnKickClearsTargetsWithWarning)
{
    auto msgs = snapshot(
        {chatMessage(QStringLiteral("badactor"), QStringLiteral("spam"),
                     NOW.addSecs(-10))});

    LimerinoMatcher content(QStringLiteral("spam"), false, true);
    LimerinoMatcher sender;

    auto plan = buildPlan(msgs, MessagePlatform::Kick, CHANNEL, SELF, content,
                          sender, 600, NukeAction::Warn, NOW);

    EXPECT_TRUE(plan.targets.isEmpty());
    ASSERT_FALSE(plan.warnings.isEmpty());
    EXPECT_TRUE(plan.warnings.last().contains(QStringLiteral("Kick")));
}
