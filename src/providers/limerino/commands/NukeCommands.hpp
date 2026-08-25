// SPDX-License-Identifier: MIT
// /unnuke (batch N4). Split into its own header so the registration list in
// LimerinoCommands.cpp stays tidy.

#pragma once

#include <QString>

namespace chatterino {

struct CommandContext;

namespace LimerinoCommands {

/// Undo the reversible portion (ban/timeout) of the most recent completed
/// nuke in this channel. Deletes are never reversible.
QString unnuke(const CommandContext &ctx);

}  // namespace LimerinoCommands
}  // namespace chatterino
