// SPDX-License-Identifier: MIT

#include "providers/limerino/crossban/CrossbanStrike.hpp"
#include "providers/limerino/crossban/CrossbanComments.hpp"
#include "providers/limerino/crossban/CrossbanPresets.hpp"

#include <gtest/gtest.h>
#include <QJsonDocument>
#include <QJsonObject>
#include <QUuid>

using namespace chatterino::limerino;

TEST(LimerinoCrossban, ParseCleanStrike)
{
    const auto doc = QJsonDocument::fromJson(R"JSON({
      "chatModeratorStrikeStatus": {
        "banDetails": null,
        "timeoutDetails": null,
        "warningDetails": null
      }
    })JSON");
    const auto strike = parseStrikeStatus(doc.object());
    EXPECT_EQ(strike.kind, CrossbanStrikeKind::Clean);
    EXPECT_EQ(strike.statusLabel(), QStringLiteral("Clean"));
}

TEST(LimerinoCrossban, ParseBannedStrike)
{
    const auto doc = QJsonDocument::fromJson(R"JSON({
      "chatModeratorStrikeStatus": {
        "banDetails": {
          "id": "BAN__1__2__3",
          "bannedBy": {"id": "1", "displayName": "Lime", "login": "lime"},
          "createdAt": "2026-08-07T03:10:46Z",
          "reason": "testmodcomment"
        },
        "timeoutDetails": null,
        "warningDetails": null
      }
    })JSON");
    const auto strike = parseStrikeStatus(doc.object());
    EXPECT_EQ(strike.kind, CrossbanStrikeKind::Banned);
    EXPECT_EQ(strike.reason, QStringLiteral("testmodcomment"));
    EXPECT_EQ(strike.actorLogin, QStringLiteral("lime"));
    EXPECT_TRUE(strike.detailLabel().contains(QStringLiteral("testmodcomment")));
}

TEST(LimerinoCrossban, ParseTimeoutStrike)
{
    const auto doc = QJsonDocument::fromJson(R"JSON({
      "chatModeratorStrikeStatus": {
        "banDetails": null,
        "timeoutDetails": {
          "id": "TIMEOUT__1__2__3",
          "timedOutBy": {"id": "1", "displayName": "Lime", "login": "lime"},
          "createdAt": "2026-08-07T03:11:35Z",
          "expiresAt": "2026-08-07T03:21:35Z",
          "expiresInMs": 500212,
          "reason": "test"
        },
        "warningDetails": null
      }
    })JSON");
    const auto strike = parseStrikeStatus(doc.object());
    EXPECT_EQ(strike.kind, CrossbanStrikeKind::TimedOut);
    EXPECT_EQ(strike.expiresAt, QStringLiteral("2026-08-07T03:21:35Z"));
    EXPECT_EQ(strike.expiresInMs, 500212);
    EXPECT_EQ(strike.reason, QStringLiteral("test"));
}

TEST(LimerinoCrossban, ParseModComments)
{
    const auto doc = QJsonDocument::fromJson(R"JSON({
      "viewerCardModLogs": {
        "comments": {
          "edges": [
            {
              "node": {
                "id": "ea17",
                "timestamp": "2026-08-07T02:55:33Z",
                "text": "abcdef",
                "author": {
                  "id": "1",
                  "login": "lime",
                  "displayName": "Lime"
                },
                "isShareable": true
              }
            }
          ]
        }
      }
    })JSON");
    const auto comments = parseModComments(doc.object());
    ASSERT_EQ(comments.size(), 1);
    EXPECT_EQ(comments[0].text, QStringLiteral("abcdef"));
    EXPECT_EQ(comments[0].authorLogin, QStringLiteral("lime"));
}

TEST(LimerinoCrossban, EnsureAllModeratedOnEmpty)
{
    QVector<CrossbanPreset> presets;
    EXPECT_TRUE(ensureAllModeratedPreset(presets));
    ASSERT_EQ(presets.size(), 1);
    EXPECT_TRUE(presets[0].useAllModeratedChannels);
    EXPECT_EQ(presets[0].id, allModeratedChannelsPresetId());
    EXPECT_TRUE(presets[0].channels.isEmpty());
    // Second call is a no-op.
    EXPECT_FALSE(ensureAllModeratedPreset(presets));
    EXPECT_EQ(presets.size(), 1);
}

TEST(LimerinoCrossban, EnsureAllModeratedKeepsCustomPresets)
{
    CrossbanPreset custom;
    custom.id = QUuid::createUuid();
    custom.name = QStringLiteral("My mods snapshot");
    custom.channels.append(
        CrossbanChannel{QStringLiteral("1"), QStringLiteral("forsen"),
                        QStringLiteral("Forsen")});

    QVector<CrossbanPreset> presets{custom};
    EXPECT_TRUE(ensureAllModeratedPreset(presets));
    ASSERT_EQ(presets.size(), 2);
    EXPECT_TRUE(presets[0].useAllModeratedChannels);
    EXPECT_EQ(presets[1].id, custom.id);
    EXPECT_EQ(presets[1].channels.size(), 1);
    EXPECT_EQ(presets[1].channels[0].login, QStringLiteral("forsen"));
}
