// SPDX-License-Identifier: MIT
// E7: chat-colour parse / normalize / recents MRU (no Settings singleton).

#include "providers/limerino/appearance/LimerinoChatColor.hpp"

#include <gtest/gtest.h>

using namespace chatterino::limerino;

TEST(LimerinoChatColor, LegacyHelixNameLoads)
{
    EXPECT_TRUE(isHelixNamedColor(QStringLiteral("blue")));
    EXPECT_TRUE(isHelixNamedColor(QStringLiteral("BlueViolet")));
    EXPECT_TRUE(isHelixNamedColor(QStringLiteral("blue_violet")));

    const QColor blue = parseChatColor(QStringLiteral("blue"));
    ASSERT_TRUE(blue.isValid());
    EXPECT_EQ(normalizeChatColorValue(QStringLiteral("Blue")),
              QStringLiteral("blue"));
    EXPECT_EQ(normalizeChatColorValue(QStringLiteral("blueviolet")),
              QStringLiteral("blue_violet"));
}

TEST(LimerinoChatColor, HexRoundTripRgbAndArgb)
{
    EXPECT_EQ(normalizeChatColorValue(QStringLiteral("#abc")),
              QStringLiteral("#aabbcc"));
    EXPECT_EQ(normalizeChatColorValue(QStringLiteral("#AABBCC")),
              QStringLiteral("#aabbcc"));

    const QColor withAlpha = parseChatColor(QStringLiteral("#80ff0000"));
    ASSERT_TRUE(withAlpha.isValid());
    EXPECT_EQ(withAlpha.alpha(), 0x80);
    EXPECT_EQ(withAlpha.red(), 0xff);
    EXPECT_EQ(chatColorHexForDisplay(withAlpha),
              QStringLiteral("#80ff0000"));
    EXPECT_EQ(normalizeChatColorValue(QStringLiteral("#80FF0000")),
              QStringLiteral("#80ff0000"));
}

TEST(LimerinoChatColor, RecentsCapAndDedupe)
{
    QStringList list;
    list = pushChatColorRecent(list, QStringLiteral("#ff0000"));
    list = pushChatColorRecent(list, QStringLiteral("#00ff00"));
    list = pushChatColorRecent(list, QStringLiteral("#0000ff"));
    list = pushChatColorRecent(list, QStringLiteral("#ffff00"));
    list = pushChatColorRecent(list, QStringLiteral("#ff00ff"));
    list = pushChatColorRecent(list, QStringLiteral("#00ffff"));  // 6th -> drop oldest

    ASSERT_EQ(list.size(), kChatColorRecentsLimit);
    EXPECT_EQ(list.front(), QStringLiteral("#00ffff"));
    EXPECT_FALSE(list.contains(QStringLiteral("#ff0000")));

    // Re-push an existing colour: moves to front, no duplicate.
    list = pushChatColorRecent(list, QStringLiteral("#0000ff"));
    EXPECT_EQ(list.front(), QStringLiteral("#0000ff"));
    EXPECT_EQ(list.count(QStringLiteral("#0000ff")), 1);
    EXPECT_EQ(list.size(), kChatColorRecentsLimit);
}

TEST(LimerinoChatColor, InvalidRejected)
{
    EXPECT_FALSE(parseChatColor(QStringLiteral("not-a-colour")).isValid());
    EXPECT_TRUE(normalizeChatColorValue(QStringLiteral("not-a-colour")).isEmpty());
    QStringList list{QStringLiteral("#ffffff")};
    list = pushChatColorRecent(list, QStringLiteral("bogus"));
    EXPECT_EQ(list, (QStringList{QStringLiteral("#ffffff")}));
}
