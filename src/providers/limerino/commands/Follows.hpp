// SPDX-License-Identifier: MIT
// Batch 2 (Follows): /follow command, Follower's / Following windows, menu shims.
// Bodies transcribed from pluginforreference/init.lua + requests.lua.

#pragma once

#include "common/Channel.hpp"

#include <QString>

namespace chatterino {

struct CommandContext;
class Split;

namespace LimerinoCommands {

// Plugin's followUser: default target is the current channel; arg resolves a login.
QString follow(const CommandContext &ctx);

// Shared follow implementation for the split menu ("Follow channel")
void followChannelFromMenu(ChannelPtr channel);

// Chat-settings-menu entries (own channel only)
void openFollowerListFor(Split *split);
void openFollowingListFor(Split *split);

}  // namespace LimerinoCommands
}  // namespace chatterino
