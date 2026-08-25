// SPDX-License-Identifier: MIT
// State-machine tests for the Hermes live-updates controller. No network: a
// recording IPubSubSink double drives the transport signals.

#include "providers/limerino/pubsub/LimerinoPubSubController.hpp"

#include "providers/limerino/LimerinoAuth.hpp"
#include "providers/limerino/pubsub/HermesChannelTopics.hpp"
#include "providers/limerino/pubsub/HermesMessages.hpp"
#include "providers/limerino/pubsub/HermesUserTopics.hpp"
#include "providers/limerino/pubsub/LimerinoPubSubEventDedupe.hpp"
#include "Test.hpp"

#include <QJsonArray>
#include <QJsonObject>
#include <QString>
#include <QtCore/qtestsupport_core.h>

#include <vector>

using namespace chatterino;
using namespace chatterino::limerino;
using namespace std::chrono_literals;

namespace {

class FakeSink : public IPubSubSink
{
public:
    struct Call {
        QString how;  // "listen" | "unlisten" | "relisten" | "retryAuth"
        QString topic;
        QString key;
    };

    std::vector<Call> calls;
    std::unordered_map<QString, QString> subs;

    size_t countTopic(const QString &how, const QString &topic) const
    {
        size_t n = 0;
        for (const auto &c : this->calls)
        {
            if (c.how == how && c.topic == topic)
            {
                n++;
            }
        }
        return n;
    }

    void listen(const QString &topic, const QString &tokenKey) override
    {
        this->calls.push_back({"listen", topic, tokenKey});
        this->subs[topic] = tokenKey;
    }
    void unlisten(const QString &topic) override
    {
        this->calls.push_back({"unlisten", topic, {}});
        this->subs.erase(topic);
    }
    void relisten(const QString &topic, const QString &tokenKey) override
    {
        this->calls.push_back({"relisten", topic, tokenKey});
        this->subs[topic] = tokenKey;
    }

    std::unordered_map<QString, QString> subscribedTopics() const override
    {
        return this->subs;
    }

    PubSubTransportStatus transportStatus() const override
    {
        return {};
    }

    void retryAuthentication(const QString &tokenKey) override
    {
        this->calls.push_back({"retryAuth", {}, tokenKey});
    }

    pajlada::Signals::Signal<const QString &> sigSubscribeSucceeded;
    pajlada::Signals::Signal<const QString &, const QString &>
        sigSubscribeFailed;
    pajlada::Signals::Signal<const QString &, const QString &> sigAuthSucceeded;
    pajlada::Signals::Signal<const QString &, const QString &> sigAuthFailed;
    pajlada::Signals::Signal<const QString &, const QString &>
        sigAuthUnavailable;
    pajlada::Signals::Signal<const QString &, const QJsonObject &>
        sigTopicMessage;

    pajlada::Signals::Signal<const QString &> &subscribeSucceeded() override
    {
        return this->sigSubscribeSucceeded;
    }
    pajlada::Signals::Signal<const QString &, const QString &>
        &subscribeFailed() override
    {
        return this->sigSubscribeFailed;
    }
    pajlada::Signals::Signal<const QString &, const QString &>
        &authSucceeded() override
    {
        return this->sigAuthSucceeded;
    }
    pajlada::Signals::Signal<const QString &, const QString &>
        &authFailed() override
    {
        return this->sigAuthFailed;
    }
    pajlada::Signals::Signal<const QString &, const QString &>
        &authUnavailable() override
    {
        return this->sigAuthUnavailable;
    }
    pajlada::Signals::Signal<const QString &, const QJsonObject &>
        &topicMessage() override
    {
        return this->sigTopicMessage;
    }
};

constexpr LimerinoPubSubController::Config TEST_CONFIG{
    std::chrono::milliseconds(20), 3, 2};

const LimerinoPubSubController::TopicStatus *statusOf(
    const LimerinoPubSubController *controller, const QString &topic)
{
    // Lookup helper: statuses don't keep insertion order.
    static std::vector<LimerinoPubSubController::TopicStatus> scratch;
    scratch = controller->topicStatuses();
    for (const auto &s : scratch)
    {
        if (s.topic == topic)
        {
            return &s;
        }
    }
    return nullptr;
}

}  // namespace

TEST(LimerinoPubSubController, UnauthTopicActivates)
{
    auto sink = std::make_unique<FakeSink>();
    auto *sinkPtr = sink.get();
    LimerinoPubSubController controller(
        std::move(sink), [](PubSubTopicAuth) -> PubSubTokenResolution {
            return {};
        },
        TEST_CONFIG);

    controller.ensureTopic("raid.1234", PubSubTopicAuth::None);
    ASSERT_EQ(sinkPtr->countTopic("listen", "raid.1234"), 1);
    ASSERT_TRUE(sinkPtr->subs.at("raid.1234") == QStringLiteral(""));

    sinkPtr->sigSubscribeSucceeded.invoke("raid.1234");
    const auto *status = statusOf(&controller, "raid.1234");
    ASSERT_TRUE(status->state == LimerinoPubSubController::TopicState::Active);
    ASSERT_EQ(status->attempts, 0);
}

TEST(LimerinoPubSubController, FailedSubscribeRetriesThenSucceeds)
{
    auto sink = std::make_unique<FakeSink>();
    auto *sinkPtr = sink.get();
    LimerinoPubSubController controller(
        std::move(sink), [](PubSubTopicAuth) -> PubSubTokenResolution {
            return {};
        },
        TEST_CONFIG);

    controller.ensureTopic("raid.1234", PubSubTopicAuth::None);

    sinkPtr->sigSubscribeFailed.invoke("raid.1234", "boom");
    const auto *status = statusOf(&controller, "raid.1234");
    ASSERT_TRUE(status->state ==
                LimerinoPubSubController::TopicState::Retrying);
    ASSERT_EQ(status->attempts, 1);

    QTest::qWait(200);
    ASSERT_EQ(sinkPtr->countTopic("relisten", "raid.1234"), 1);

    sinkPtr->sigSubscribeSucceeded.invoke("raid.1234");
    status = statusOf(&controller, "raid.1234");
    ASSERT_TRUE(status->state == LimerinoPubSubController::TopicState::Active);
    ASSERT_EQ(status->attempts, 0);
}

TEST(LimerinoPubSubController, FailedSubscribeTerminatesAndManualRetry)
{
    auto sink = std::make_unique<FakeSink>();
    auto *sinkPtr = sink.get();
    LimerinoPubSubController controller(
        std::move(sink), [](PubSubTopicAuth) -> PubSubTokenResolution {
            return {};
        },
        TEST_CONFIG);

    controller.ensureTopic("raid.1234", PubSubTopicAuth::None);

    for (int i = 0; i < 3; ++i)  // TEST_CONFIG.maxAttempts = 3
    {
        sinkPtr->sigSubscribeFailed.invoke("raid.1234", "err");
        QTest::qWait(100);  // let any retry fire
    }
    const auto *status = statusOf(&controller, "raid.1234");
    ASSERT_TRUE(status->state == LimerinoPubSubController::TopicState::Failed);
    ASSERT_EQ(status->attempts, 3);
    ASSERT_TRUE(status->lastError == QStringLiteral("err"));

    // Terminal failure produces a visible diagnostics delta.
    auto snapshot = controller.diagSnapshot();
    ASSERT_EQ(snapshot.topicsFailed, 1);
    ASSERT_FALSE(snapshot.lastError.isEmpty());

    controller.retryFailed();
    status = statusOf(&controller, "raid.1234");
    ASSERT_TRUE(status->state == LimerinoPubSubController::TopicState::Pending);
    ASSERT_EQ(status->attempts, 0);
    ASSERT_GE(sinkPtr->countTopic("listen", "raid.1234"), 2);
}

TEST(LimerinoPubSubController, UserTopicBlockedWithoutAuthThenUnblocked)
{
    bool resolvable = false;
    auto sink = std::make_unique<FakeSink>();
    auto *sinkPtr = sink.get();
    LimerinoPubSubController controller(
        std::move(sink),
        [&resolvable](PubSubTopicAuth) -> PubSubTokenResolution {
            if (resolvable)
            {
                return {"tok", "u42", {}};
            }
            return {{}, {}, "no account"};
        },
        TEST_CONFIG);

    controller.ensureTopic("chatrooms-user-v1.u42", PubSubTopicAuth::User);
    ASSERT_EQ(sinkPtr->calls.size(), 0);  // never submitted (rule 5)
    const auto *status = statusOf(&controller, "chatrooms-user-v1.u42");
    ASSERT_TRUE(status->state ==
                LimerinoPubSubController::TopicState::AuthBlocked);
    ASSERT_TRUE(status->lastError == QStringLiteral("no account"));

    // Account appears: reconcile un-blocks and subscribes with the new key.
    resolvable = true;
    LimerinoAuth::accountsChanged.invoke();
    ASSERT_EQ(sinkPtr->countTopic("listen", "chatrooms-user-v1.u42"), 1);
    ASSERT_TRUE(sinkPtr->subs.at("chatrooms-user-v1.u42") ==
                QStringLiteral("u42"));
    status = statusOf(&controller, "chatrooms-user-v1.u42");
    ASSERT_TRUE(status->state == LimerinoPubSubController::TopicState::Pending);
}

TEST(LimerinoPubSubController, AccountSwitchRekeysTopic)
{
    QString currentUserId = "u1";
    auto sink = std::make_unique<FakeSink>();
    auto *sinkPtr = sink.get();
    LimerinoPubSubController controller(
        std::move(sink),
        [&currentUserId](PubSubTopicAuth) -> PubSubTokenResolution {
            if (currentUserId.isEmpty())
            {
                return {{}, {}, "no account"};
            }
            return {"tok", currentUserId, {}};
        },
        TEST_CONFIG);

    controller.ensureTopic("chatrooms-user-v1.u1", PubSubTopicAuth::User);
    ASSERT_TRUE(sinkPtr->subs.at("chatrooms-user-v1.u1") ==
                QStringLiteral("u1"));

    // Switch: the topic is re-issued as the new user's own topic string.
    currentUserId = "u2";
    LimerinoAuth::accountsChanged.invoke();
    ASSERT_EQ(sinkPtr->countTopic("unlisten", "chatrooms-user-v1.u1"), 1);
    ASSERT_EQ(sinkPtr->subs.count("chatrooms-user-v1.u1"), 0);
    ASSERT_TRUE(sinkPtr->subs.at("chatrooms-user-v1.u2") ==
                QStringLiteral("u2"));

    // Account disappears: the topic must be unsubscribed entirely (rule 5).
    currentUserId.clear();
    LimerinoAuth::accountsChanged.invoke();
    ASSERT_EQ(sinkPtr->countTopic("unlisten", "chatrooms-user-v1.u2"), 1);
    ASSERT_EQ(sinkPtr->subs.count("chatrooms-user-v1.u2"), 0);
}

TEST(LimerinoPubSubController, SweepRemovesForeignUserTopics)
{
    auto sink = std::make_unique<FakeSink>();
    auto *sinkPtr = sink.get();
    LimerinoPubSubController controller(
        std::move(sink), [](PubSubTopicAuth) -> PubSubTokenResolution {
            return {"tok", "u1", {}};
        },
        TEST_CONFIG);

    controller.ensureTopic("chatrooms-user-v1.u1", PubSubTopicAuth::User);
    controller.ensureTopic("raid.1234", PubSubTopicAuth::None);

    // A stale subscription from another account lingers on the connection.
    sinkPtr->subs["chatrooms-user-v1.u999"] = "u999";

    LimerinoAuth::accountsChanged.invoke();

    ASSERT_EQ(sinkPtr->countTopic("unlisten", "chatrooms-user-v1.u999"), 1);
    ASSERT_EQ(sinkPtr->subs.count("chatrooms-user-v1.u999"), 0);
    // Our own topics stay untouched.
    ASSERT_EQ(sinkPtr->subs.count("chatrooms-user-v1.u1"), 1);
    ASSERT_EQ(sinkPtr->subs.count("raid.1234"), 1);
}

TEST(LimerinoPubSubController, AuthFailuresFailTopicFast)
{
    auto sink = std::make_unique<FakeSink>();
    auto *sinkPtr = sink.get();
    LimerinoPubSubController controller(
        std::move(sink), [](PubSubTopicAuth) -> PubSubTokenResolution {
            return {"tok", "u1", {}};
        },
        TEST_CONFIG);

    controller.ensureTopic("chatrooms-user-v1.u1", PubSubTopicAuth::User);

    sinkPtr->sigAuthFailed.invoke("u1", "ERR");
    const auto *status = statusOf(&controller, "chatrooms-user-v1.u1");
    ASSERT_TRUE(status->state == LimerinoPubSubController::TopicState::Pending);

    sinkPtr->sigAuthFailed.invoke("u1", "ERR");  // TEST_CONFIG.maxAuthFailures = 2
    status = statusOf(&controller, "chatrooms-user-v1.u1");
    ASSERT_TRUE(status->state == LimerinoPubSubController::TopicState::Failed);
    ASSERT_TRUE(status->lastError == QStringLiteral("ERR"));

    // The auth gate now refuses live token resolves for this key.
    QString reason;
    ASSERT_FALSE(controller.resolveLiveToken("u1", &reason).has_value());
    ASSERT_FALSE(reason.isEmpty());

    controller.retryFailed();
    status = statusOf(&controller, "chatrooms-user-v1.u1");
    ASSERT_TRUE(status->state == LimerinoPubSubController::TopicState::Pending);
}

TEST(LimerinoPubSubController, NotificationBecomesGenericEvent)
{
    auto sink = std::make_unique<FakeSink>();
    auto *sinkPtr = sink.get();
    LimerinoPubSubController controller(
        std::move(sink), [](PubSubTopicAuth) -> PubSubTokenResolution {
            return {};
        },
        TEST_CONFIG);

    controller.ensureTopic("raid.1234", PubSubTopicAuth::None);

    std::vector<PubSubEvent> events;
    controller.eventProduced.connect(
        [&events](const PubSubEvent &event) { events.push_back(event); });

    QJsonObject payload{{"type", "raid_update_v2"},
                        {"raid", QJsonObject{{"id", "r1"}}}};
    sinkPtr->sigTopicMessage.invoke("raid.1234", payload);

    ASSERT_EQ(events.size(), 1);
    ASSERT_TRUE(events[0].topic == QStringLiteral("raid.1234"));
    ASSERT_TRUE(events[0].channelId == QStringLiteral("1234"));
    ASSERT_TRUE(events[0].eventType == QStringLiteral("raid_update_v2"));
    ASSERT_FALSE(events[0].displayText.isEmpty());
    ASSERT_TRUE(
        controller.knownEventTypes().contains(QStringLiteral("raid_update_v2")));
}

TEST(LimerinoPubSubController, RegisteredHandlerFormatsEvent)
{
    auto sink = std::make_unique<FakeSink>();
    auto *sinkPtr = sink.get();
    LimerinoPubSubController controller(
        std::move(sink), [](PubSubTopicAuth) -> PubSubTokenResolution {
            return {};
        },
        TEST_CONFIG);

    controller.registerTopicHandler(
        "raid.",
        [](const QString &, const QJsonObject &payload, PubSubEvent &event) {
            event.displayText = QStringLiteral("raid event %1")
                                    .arg(payload["type"].toString());
            return true;
        });

    std::vector<PubSubEvent> events;
    controller.eventProduced.connect(
        [&events](const PubSubEvent &event) { events.push_back(event); });

    sinkPtr->sigTopicMessage.invoke("raid.1234",
                                    QJsonObject{{"type", "raid_update_v2"}});

    ASSERT_EQ(events.size(), 1);
    ASSERT_TRUE(events[0].displayText ==
                QStringLiteral("raid event raid_update_v2"));
}

// ---- P1: channel-topic handlers ----

TEST(LimerinoPubSubP1, HandlersInstallAndRaidFormats)
{
    auto sink = std::make_unique<FakeSink>();
    auto *sinkPtr = sink.get();
    LimerinoPubSubController controller(
        std::move(sink), [](PubSubTopicAuth) -> PubSubTokenResolution {
            return {};
        },
        TEST_CONFIG);

    limerino::installHermesChannelTopicHandlers(controller);

    std::vector<PubSubEvent> events;
    controller.eventProduced.connect(
        [&events](const PubSubEvent &event) { events.push_back(event); });

    const QJsonObject raid{
        {"id", "raid-1"},
        {"source_id", "9001"},
        {"target_login", "target"},
        {"target_display_name", "Target"},
    };

    // Reference shape: raid at top level (events.js L79).
    sinkPtr->sigTopicMessage.invoke(
        "raid.2500",
        QJsonObject{{"type", "raid_update_v2"}, {"raid", raid}});

    ASSERT_EQ(events.size(), 1);
    ASSERT_TRUE(events[0].displayText.contains(QStringLiteral("Target")));
    ASSERT_TRUE(events[0].displayText.contains(QStringLiteral("id:9001")));
    ASSERT_TRUE(events[0].channelId == QStringLiteral("2500"));
    ASSERT_TRUE(events[0].displayChannelId == QStringLiteral("9001"));

    // Nested data.raid shape (same raid id → still a second line when Settings
    // / dedupe is unavailable in this test harness).
    sinkPtr->sigTopicMessage.invoke(
        "raid.2500",
        QJsonObject{{"type", "raid_update_v2"},
                    {"data", QJsonObject{{"raid", raid}}}});
    ASSERT_EQ(events.size(), 2);
    ASSERT_TRUE(events[1].displayText.contains(QStringLiteral("Target")));

    // raid_go_v2 is distinct from raid_update_v2 despite sharing raid.id.
    sinkPtr->sigTopicMessage.invoke(
        "raid.2500",
        QJsonObject{{"type", "raid_go_v2"}, {"raid", raid}});
    ASSERT_EQ(events.size(), 3);
    ASSERT_TRUE(events[2].displayText.contains(QStringLiteral("raid started")));
}

TEST(LimerinoPubSubP1, PollCreateListsChoicesWithoutZeroCounts)
{
    auto sink = std::make_unique<FakeSink>();
    auto *sinkPtr = sink.get();
    LimerinoPubSubController controller(
        std::move(sink), [](PubSubTopicAuth) -> PubSubTokenResolution {
            return {};
        },
        TEST_CONFIG);

    limerino::installHermesChannelTopicHandlers(controller);

    std::vector<PubSubEvent> events;
    controller.eventProduced.connect(
        [&events](const PubSubEvent &event) { events.push_back(event); });

    const QJsonObject poll{
        {"poll_id", "p1"},
        {"title", "aa"},
        {"status", "ACTIVE"},
        {"settings",
         QJsonObject{{"multi_choice", QJsonObject{{"is_enabled", true}}}}},
        {"choices",
         QJsonArray{
             QJsonObject{{"title", "1"},
                         {"votes", QJsonObject{{"total", 0}}}},
             QJsonObject{{"title", "2"},
                         {"votes", QJsonObject{{"total", 0}}}},
         }},
    };
    sinkPtr->sigTopicMessage.invoke(
        "polls.99",
        QJsonObject{{"type", "POLL_CREATE"},
                    {"data", QJsonObject{{"poll", poll}}}});

    ASSERT_EQ(events.size(), 1);
    ASSERT_TRUE(events[0].displayText.contains(QStringLiteral("aa")));
    ASSERT_TRUE(events[0].displayText.contains(QStringLiteral("1. 1")));
    ASSERT_TRUE(events[0].displayText.contains(QStringLiteral("2. 2")));
    ASSERT_TRUE(events[0].displayText.contains(QStringLiteral("multi")));
    ASSERT_FALSE(events[0].displayText.contains(QStringLiteral("(0)")));
}

TEST(LimerinoPubSubP1, UnknownTypesFallBackToTypeString)
{
    auto sink = std::make_unique<FakeSink>();
    auto *sinkPtr = sink.get();
    LimerinoPubSubController controller(
        std::move(sink), [](PubSubTopicAuth) -> PubSubTokenResolution {
            return {};
        },
        TEST_CONFIG);

    limerino::installHermesChannelTopicHandlers(controller);

    std::vector<PubSubEvent> events;
    controller.eventProduced.connect(
        [&events](const PubSubEvent &event) { events.push_back(event); });

    sinkPtr->sigTopicMessage.invoke(
        "polls.99", QJsonObject{{"type", "weird-poll-thing"}});

    ASSERT_EQ(events.size(), 1);
    ASSERT_TRUE(events[0].displayText.contains(QStringLiteral("weird-poll-thing")));
}

// ---- P2: user-topic handlers ----

TEST(LimerinoPubSubP2, UserModerationActionFormatsAndFilters)
{
    auto sink = std::make_unique<FakeSink>();
    auto *sinkPtr = sink.get();
    LimerinoPubSubController controller(
        std::move(sink), [](PubSubTopicAuth) -> PubSubTokenResolution {
            return {"tok", "u1", {}};
        },
        TEST_CONFIG);

    limerino::installHermesUserTopicHandlers(controller);

    std::vector<PubSubEvent> events;
    controller.eventProduced.connect(
        [&events](const PubSubEvent &event) { events.push_back(event); });

    // events.js L21 shape: type / data.{action,channel_id,target_id,reason}.
    sinkPtr->sigTopicMessage.invoke(
        "chatrooms-user-v1.u1",
        QJsonObject{
            {"type", "user_moderation_action"},
            {"data",
             QJsonObject{{"action", "warn"},
                         {"channel_id", "12345"},
                         {"target_id", "u1"},
                         {"reason", "add spam to whitelist"}}}});

    ASSERT_EQ(events.size(), 1);
    ASSERT_TRUE(events[0].eventType == QStringLiteral("user_moderation_action"));
    ASSERT_TRUE(
        events[0].displayText.contains(QStringLiteral("add spam to whitelist")));
    // Known to the filter dialog from the moment of registration:
    ASSERT_TRUE(controller.knownEventTypes().contains(
        QStringLiteral("user_moderation_action")));

    // An action NOT in the specification allowlist is not treated specially.
    sinkPtr->sigTopicMessage.invoke(
        "chatrooms-user-v1.u1",
        QJsonObject{
            {"type", "user_moderation_action"},
            {"data",
             QJsonObject{{"action", "some_other_action"},
                         {"channel_id", "12345"},
                         {"target_id", "u1"}}}});
    ASSERT_EQ(events.size(), 2);
    // falls through to generic display (topic + type):
    ASSERT_TRUE(events[1].displayText.contains(
        QStringLiteral("user_moderation_action")));
    ASSERT_TRUE(events[1].displayText.contains(QStringLiteral("chatrooms-user")));

    // targeting a different user is dropped (events.js L27 filter)
    sinkPtr->sigTopicMessage.invoke(
        "chatrooms-user-v1.u1",
        QJsonObject{
            {"type", "user_moderation_action"},
            {"data",
             QJsonObject{{"action", "warn"},
                         {"channel_id", "12345"},
                         {"target_id", "not-us"}}}});
    ASSERT_EQ(events.size(), 2);
}

TEST(LimerinoPubSubP2, PointsSpentShowsNewBalance)
{
    auto sink = std::make_unique<FakeSink>();
    auto *sinkPtr = sink.get();
    LimerinoPubSubController controller(
        std::move(sink), [](PubSubTopicAuth) -> PubSubTokenResolution {
            return {"tok", "u1", {}};
        },
        TEST_CONFIG);

    limerino::installHermesUserTopicHandlers(controller);

    std::vector<PubSubEvent> events;
    controller.eventProduced.connect(
        [&events](const PubSubEvent &event) { events.push_back(event); });

    sinkPtr->sigTopicMessage.invoke(
        "community-points-user-v1.u1",
        QJsonObject{
            {"type", "points-spent"},
            {"data",
             QJsonObject{
                 {"timestamp", "2026-08-01T22:00:00Z"},
                 {"balance",
                  QJsonObject{{"user_id", "u1"},
                              {"channel_id", "12345"},
                              {"balance", 432}}}}}});

    ASSERT_EQ(events.size(), 1);
    ASSERT_TRUE(
        events[0].displayText.contains(QStringLiteral("432")));

    // Unknown type on the same topic falls through unhandled.
    sinkPtr->sigTopicMessage.invoke(
        "community-points-user-v1.u1",
        QJsonObject{{"type", "points-earned"}});
    ASSERT_TRUE(events[1].displayText.contains(QStringLiteral("points-earned")));
}

// ---- P3 addition: follows user topic ----

TEST(LimerinoPubSubP3, FollowsFollowAndUnfollow)
{
    auto sink = std::make_unique<FakeSink>();
    auto *sinkPtr = sink.get();
    LimerinoPubSubController controller(
        std::move(sink), [](PubSubTopicAuth) -> PubSubTokenResolution {
            return {"tok", "u1", {}};
        },
        TEST_CONFIG);

    limerino::installHermesUserTopicHandlers(controller);

    std::vector<PubSubEvent> events;
    controller.eventProduced.connect(
        [&events](const PubSubEvent &event) { events.push_back(event); });

    // client.js L53: followed shape.
    sinkPtr->sigTopicMessage.invoke(
        "follows.u1",
        QJsonObject{
            {"type", "user-followed"},
            {"timestamp", "2026-08-01T22:00:00Z"},
            {"target_display_name", "forsen"},
            {"target_username", "forsen"},
            {"target_user_id", "22484632"}});
    ASSERT_EQ(events.size(), 1);
    ASSERT_TRUE(events[0].displayText.contains(QStringLiteral("forsen")));

    // client.js L65: unfollowed (display name absent, id present).
    sinkPtr->sigTopicMessage.invoke(
        "follows.u1",
        QJsonObject{{"type", "user-unfollowed"},
                    {"timestamp", "2026-08-01T22:00:00Z"},
                    {"target_user_id", "22484639"}});
    ASSERT_EQ(events.size(), 2);
    ASSERT_TRUE(events[1].displayText.contains(QStringLiteral("22484639")));

    // presence example in the same user-sub family for completeness
    sinkPtr->sigTopicMessage.invoke(
        "follows.u1", QJsonObject{{"type", "presence"}});
    // presence has no handler here; falls back to type-display.
    ASSERT_TRUE(events[2].displayText.contains(QStringLiteral("presence")));
}

// ---- R5: Hermes wire-envelope parsing (newpubsubhermesreference shapes) ----

TEST(LimerinoHermesEnvelope, WelcomeDefaultsAndParses)
{
    const auto frame = parseHermesFrame(R"({
        "type":"welcome","id":"x","timestamp":"t",
        "welcome":{"keepaliveSec":12}
    })");
    ASSERT_TRUE(frame.has_value());
    ASSERT_TRUE(frame->type == HermesFrame::Type::Welcome);
    const auto welcome = parseHermesWelcome(frame->object);
    ASSERT_TRUE(welcome.has_value());
    ASSERT_EQ(welcome->keepaliveSec, 12);

    // (keepaliveSec absent) -> default 10 (client.js: msg.welcome.keepaliveSec || 10)
    // note: welcome must be a non-empty object for parseHermesWelcome to accept.
    const auto bare = parseHermesFrame(R"({"type":"welcome","welcome":{"x":1}})");
    ASSERT_TRUE(bare.has_value());
    const auto bareWelcome = parseHermesWelcome(bare->object);
    ASSERT_TRUE(bareWelcome.has_value());
    ASSERT_EQ(bareWelcome->keepaliveSec, 10);
}

TEST(LimerinoHermesEnvelope, ResultResponseShapesAndParentId)
{
    const auto frame = parseHermesFrame(R"({
        "type":"subscribeResponse","parentId":"sub-1",
        "subscribeResponse":{"result":"error","error":"too many subscriptions",
                             "errorCode":"SUB006"}
    })");
    ASSERT_TRUE(frame.has_value());
    ASSERT_TRUE(frame->type == HermesFrame::Type::SubscribeResponse);
    const auto r = parseHermesSubscribeResponse(frame->object);
    ASSERT_TRUE(r.has_value());
    ASSERT_EQ(r->result, QStringLiteral("error"));
    ASSERT_EQ(r->errorCode, QStringLiteral("SUB006"));
    const auto parent = hermesResponseParentId(frame->object);
    ASSERT_TRUE(parent.has_value());
    ASSERT_EQ(*parent, QStringLiteral("sub-1"));

    // missing parentId -> nullopt (subscribe/unsubscribe correlate via parentId)
    const auto unparented = parseHermesFrame(
        R"({"type":"unsubscribeResponse","unsubscribeResponse":{"result":"ok"}})");
    ASSERT_TRUE(unparented.has_value());
    ASSERT_TRUE(unparented->type == HermesFrame::Type::UnsubscribeResponse);
    ASSERT_TRUE(parseHermesUnsubscribeResponse(unparented->object).has_value());
    ASSERT_FALSE(hermesResponseParentId(unparented->object).has_value());
}

TEST(LimerinoHermesEnvelope, NotificationUnwrapsStringifiedPubsub)
{
    const auto frame = parseHermesFrame(R"({
        "type":"notification","id":"n1",
        "notification":{"subscription":{"id":"sub-9"},"type":"pubsub",
                        "pubsub":"{\"type\":\"points-spent\",\"data\":{\"x\":1}}"}
    })");
    ASSERT_TRUE(frame.has_value());
    ASSERT_TRUE(frame->type == HermesFrame::Type::Notification);
    const auto note = parseHermesNotification(frame->object);
    ASSERT_TRUE(note.has_value());
    ASSERT_EQ(note->subscriptionId, QStringLiteral("sub-9"));
    ASSERT_EQ(note->payload.value(QStringLiteral("type")).toString(),
              QStringLiteral("points-spent"));

    // inner payload missing .type -> dropped by both the reference and us
    const auto noType = parseHermesFrame(R"({
        "type":"notification",
        "notification":{"subscription":{"id":"s"},"pubsub":"{\"a\":1}"}
    })");
    ASSERT_TRUE(noType.has_value());
    ASSERT_FALSE(parseHermesNotification(noType->object).has_value());
}

TEST(LimerinoHermesEnvelope, FrameTypeDispatchCoversAllKnownAndInvalid)
{
    struct Case {
        const char *json;
        HermesFrame::Type expect;
    };
    const Case cases[] = {
        {R"({"type":"welcome","welcome":{}})", HermesFrame::Type::Welcome},
        {R"({"type":"keepalive"})", HermesFrame::Type::Keepalive},
        {R"({"type":"reconnect"})", HermesFrame::Type::Reconnect},
        {R"({"type":"authenticateResponse","authenticateResponse":{"result":"ok"}})",
         HermesFrame::Type::AuthenticateResponse},
        {R"({"type":"subscribeResponse","subscribeResponse":{"result":"ok"}})",
         HermesFrame::Type::SubscribeResponse},
        {R"({"type":"unsubscribeResponse","unsubscribeResponse":{"result":"ok"}})",
         HermesFrame::Type::UnsubscribeResponse},
        {R"({"type":"notification",
             "notification":{"subscription":{"id":"s"},
                             "pubsub":"{\"type\":\"x\"}"}})",
         HermesFrame::Type::Notification},
        {R"({"type":"something-unknown"})", HermesFrame::Type::INVALID},
    };
    for (const auto &c : cases)
    {
        const auto frame = parseHermesFrame(c.json);
        ASSERT_TRUE(frame.has_value());
        ASSERT_TRUE(frame->type == c.expect)
            << "unexpected type for " << c.json;
    }
    ASSERT_FALSE(parseHermesFrame("not json").has_value());
}

// ---- R2 regression: predictions-* real shapes (no silent no-op) ----

TEST(LimerinoPubSubR2, PredictionsChannelEventCreatedAndUpdated)
{
    auto sink = std::make_unique<FakeSink>();
    auto *sinkPtr = sink.get();
    LimerinoPubSubController controller(
        std::move(sink), [](PubSubTopicAuth) -> PubSubTokenResolution {
            return {};
        },
        TEST_CONFIG);

    limerino::installHermesChannelTopicHandlers(controller);

    std::vector<PubSubEvent> events;
    controller.eventProduced.connect(
        [&events](const PubSubEvent &event) { events.push_back(event); });

    const QJsonObject predictionEvent{
        {"id", "evt-1"},
        {"channel_id", "1234"},
        {"title", "Will it rain?"},
        {"status", "ACTIVE"},
        {"outcomes",
         QJsonArray{QJsonObject{{"title", "Yes"}},
                    QJsonObject{{"title", "No"}}}},
    };

    sinkPtr->sigTopicMessage.invoke(
        "predictions-channel-v1.1234",
        QJsonObject{{"type", "event-created"},
                    {"data", QJsonObject{{"event", predictionEvent}}}});
    ASSERT_EQ(events.size(), 1);
    ASSERT_TRUE(events[0].eventType == QStringLiteral("event-created"));
    ASSERT_TRUE(events[0].displayText.contains(QStringLiteral("Will it rain?")));
    // outcome titles joined, not silently dropped
    ASSERT_TRUE(events[0].displayText.contains(QStringLiteral("Yes")));
    ASSERT_TRUE(events[0].displayText.contains(QStringLiteral("No")));

    sinkPtr->sigTopicMessage.invoke(
        "predictions-channel-v1.1234",
        QJsonObject{{"type", "event-updated"},
                    {"data", QJsonObject{{"event",
                                          QJsonObject{{"title", "Will it rain?"},
                                                      {"status", "LOCKED"}}}}}});
    ASSERT_EQ(events.size(), 2);
    ASSERT_TRUE(events[1].displayText.contains(QStringLiteral("locked")));

    // an unknown prediction type on the SAME topic must not be a silent no-op:
    // falls back to the type string so it surfaces in the events channel.
    sinkPtr->sigTopicMessage.invoke(
        "predictions-channel-v1.1234",
        QJsonObject{{"type", "prediction-unexpected"}});
    ASSERT_EQ(events.size(), 3);
    ASSERT_TRUE(events[2].displayText.contains(
        QStringLiteral("prediction-unexpected")));

    ASSERT_TRUE(
        controller.knownEventTypes().contains(QStringLiteral("event-created")));
    ASSERT_TRUE(
        controller.knownEventTypes().contains(QStringLiteral("event-updated")));
}

TEST(LimerinoPubSubR2, PredictionsUserEventAndResult)
{
    auto sink = std::make_unique<FakeSink>();
    auto *sinkPtr = sink.get();
    LimerinoPubSubController controller(
        std::move(sink), [](PubSubTopicAuth) -> PubSubTokenResolution {
            return {"tok", "u1", {}};
        },
        TEST_CONFIG);

    limerino::installHermesUserTopicHandlers(controller);

    std::vector<PubSubEvent> events;
    controller.eventProduced.connect(
        [&events](const PubSubEvent &event) { events.push_back(event); });

    sinkPtr->sigTopicMessage.invoke(
        "predictions-user-v1.u1",
        QJsonObject{{"type", "event-created"},
                    {"data",
                     QJsonObject{{"event",
                                  QJsonObject{{"title", "Beat the boss"},
                                              {"outcomes",
                                               QJsonArray{QJsonObject{
                                                   {"title", "Yes"}}}}}}}}});
    ASSERT_EQ(events.size(), 1);
    ASSERT_TRUE(events[0].displayText.contains(QStringLiteral("Beat the boss")));

    // prediction-result: WIN path uses data.prediction.result.type + points
    sinkPtr->sigTopicMessage.invoke(
        "predictions-user-v1.u1",
        QJsonObject{{"type", "prediction-result"},
                    {"data",
                     QJsonObject{{"prediction",
                                  QJsonObject{{"event_id", "evt-1"},
                                              {"points", 250},
                                              {"result",
                                               QJsonObject{{"type",
                                                            "WIN"}}}}}}}});
    ASSERT_EQ(events.size(), 2);
    ASSERT_TRUE(events[1].eventType == QStringLiteral("prediction-result"));
    ASSERT_TRUE(events[1].displayText.contains(QStringLiteral("250")));

    ASSERT_TRUE(controller.knownEventTypes().contains(
        QStringLiteral("prediction-result")));
    // the stale guessed strings must be gone from the pre-registered set
    ASSERT_FALSE(controller.knownEventTypes().contains(
        QStringLiteral("prediction-event")));
    ASSERT_FALSE(controller.knownEventTypes().contains(
        QStringLiteral("prediction-prediction")));
}

TEST(LimerinoPubSubDedupe, RaidIdentitySeparatesUpdateAndGo)
{
    PubSubEvent update{
        .eventType = QStringLiteral("raid_update_v2"),
        .payload =
            QJsonObject{{"type", "raid_update_v2"},
                        {"raid", QJsonObject{{"id", "r1"},
                                             {"source_id", "1"}}}},
    };
    PubSubEvent go = update;
    go.eventType = QStringLiteral("raid_go_v2");
    go.payload["type"] = QStringLiteral("raid_go_v2");

    const QString idUpdate = pubSubEventIdentity(update);
    const QString idGo = pubSubEventIdentity(go);
    ASSERT_FALSE(idUpdate.isEmpty());
    ASSERT_NE(idUpdate, idGo);

    PubSubEventDedupe dedupe;
    ASSERT_FALSE(dedupe.isDuplicate(idUpdate, pubSubDedupeWindowMs(
                                                  update.eventType)));
    ASSERT_TRUE(dedupe.isDuplicate(idUpdate, pubSubDedupeWindowMs(
                                                 update.eventType)));
    ASSERT_FALSE(
        dedupe.isDuplicate(idGo, pubSubDedupeWindowMs(go.eventType)));
}

TEST(LimerinoPubSubDedupe, PollIdentityIncludesType)
{
    PubSubEvent create{
        .eventType = QStringLiteral("POLL_CREATE"),
        .payload =
            QJsonObject{
                {"type", "POLL_CREATE"},
                {"data",
                 QJsonObject{
                     {"poll", QJsonObject{{"poll_id", "p1"},
                                          {"status", "ACTIVE"},
                                          {"title", "aa"}}}}}},
    };
    PubSubEvent update{
        .eventType = QStringLiteral("POLL_UPDATE"),
        .payload =
            QJsonObject{
                {"type", "POLL_UPDATE"},
                {"data",
                 QJsonObject{
                     {"poll", QJsonObject{{"poll_id", "p1"},
                                          {"status", "ACTIVE"},
                                          {"title", "aa"}}}}}},
    };

    ASSERT_NE(pubSubEventIdentity(create), pubSubEventIdentity(update));
}
