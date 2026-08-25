// SPDX-License-Identifier: MIT
// Batch 10 (Moderation): /modlogs window + /acknowledgewarning.
// Bodies transcribed from pluginforreference/init.lua + requests.lua.

#pragma once

#include <QString>

namespace chatterino {

struct CommandContext;

namespace LimerinoCommands {

QString modlogs(const CommandContext &ctx);
QString acknowledgeWarning(const CommandContext &ctx);
QString cancelNuke(const CommandContext &ctx);  // batch N3

}  // namespace LimerinoCommands
}  // namespace chatterino
