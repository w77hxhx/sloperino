// SPDX-FileCopyrightText: 2017 Contributors to Chatterino <https://chatterino.com>
//
// SPDX-License-Identifier: MIT

#pragma once

#include <IrcTagsRef>
#include <QString>

namespace chatterino {

bool trimChannelName(const QString &channelName, QString &outChannelName);
int stripLeadingReplyMention(Communi::TagsRef tags, QString &content);

}  // namespace chatterino
