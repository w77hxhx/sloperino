// SPDX-License-Identifier: MIT

#include "providers/limerino/commands/NukeCommands.hpp"

#include "common/Channel.hpp"
#include "controllers/commands/CommandContext.hpp"
#include "providers/limerino/nuke/NukeExecutor.hpp"

namespace chatterino::LimerinoCommands {

QString unnuke(const CommandContext &ctx)
{
    const auto outcome = limerino::undoLastNuke(ctx.channel);
    if (ctx.channel)
    {
        ctx.channel->addSystemMessage(outcome);
    }
    return {};
}

}  // namespace chatterino::LimerinoCommands
