// SPDX-License-Identifier: MIT
// Registration entry point for the native port of the reference plugin's
// command set. Populated batch by batch (see FORK.md); each approved command
// group registers its own surfaces here.

#pragma once

namespace chatterino {

class CommandController;

namespace LimerinoCommands {

// Called once from CommandController initialization (one upstream hook line,
// added when batch 1 lands the first real command).
void initialize(CommandController &commands);

}  // namespace LimerinoCommands
}  // namespace chatterino
