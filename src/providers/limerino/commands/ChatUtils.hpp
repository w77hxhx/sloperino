// SPDX-License-Identifier: MIT
// Batch 11/12 (Chat utils): /resub, /cheer, /displayname, /logsextended.
// Bodies transcribed from pluginforreference/init.lua + requests.lua.

#pragma once

#include <QString>

namespace chatterino {

struct CommandContext;

namespace LimerinoCommands {

QString resubNotification(const CommandContext &ctx);
QString cheer(const CommandContext &ctx);
QString displayName(const CommandContext &ctx);
QString logsExtended(const CommandContext &ctx);

}  // namespace LimerinoCommands
}  // namespace chatterino
