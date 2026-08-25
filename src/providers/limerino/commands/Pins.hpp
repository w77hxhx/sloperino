// SPDX-License-Identifier: MIT
// Batch 3 (Pins): /pinmessage, /sendpinnedmessage, /unpin, /viewpin (+/getpin).
// Bodies transcribed from pluginforreference/init.lua + requests.lua;
// the /viewpin surface is the user-approved split-top banner.

#pragma once

#include <QString>

namespace chatterino {

struct CommandContext;

namespace LimerinoCommands {

QString pinMessage(const CommandContext &ctx);
QString sendPinnedMessage(const CommandContext &ctx);
QString unpinMessage(const CommandContext &ctx);
QString viewPin(const CommandContext &ctx);

}  // namespace LimerinoCommands
}  // namespace chatterino
