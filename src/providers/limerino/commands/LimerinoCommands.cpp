// SPDX-License-Identifier: MIT

#include "providers/limerino/commands/LimerinoCommands.hpp"

#include "controllers/commands/CommandController.hpp"
#include "controllers/commands/CommandContext.hpp"
#include "providers/limerino/commands/Follows.hpp"
#include "providers/limerino/commands/Identity.hpp"
#include "providers/limerino/commands/ChatUtils.hpp"
#include "providers/limerino/commands/Moderation.hpp"
#include "providers/limerino/commands/NukeCommands.hpp"
#include "providers/limerino/commands/Pins.hpp"
#include "providers/limerino/commands/Roles.hpp"

namespace chatterino::LimerinoCommands {

void initialize(CommandController &commands)
{
    // Batch 1 - Identity & account
    commands.registerExternalCommand("/uid", &uid);
    commands.registerExternalCommand("/namehistory", &nameHistory);
    commands.registerExternalCommand("/modlist", &modList);
    commands.registerExternalCommand("/ml", &modList);

    // Batch 2 - Follows (replaces upstream's deprecated /follow builtin)
    commands.registerExternalCommand("/follow", &follow);

    // Batch 3 - Pins
    commands.registerExternalCommand("/pinmessage", &pinMessage);
    commands.registerExternalCommand("/sendpinnedmessage", &sendPinnedMessage);
    commands.registerExternalCommand("/viewpin", &viewPin);
    commands.registerExternalCommand("/getpin", &viewPin);
    commands.registerExternalCommand("/unpin", &unpinMessage);

    // Batch 9 - Roles (artist/leadmod usercard buttons are own-channel-only,
    // decided in UserInfoPopup.cpp)
    commands.registerExternalCommand("/artist", &grantArtistCmd);
    commands.registerExternalCommand("/unartist", &revokeArtistCmd);
    commands.registerExternalCommand("/leadmod", &grantLeadModCmd);

    // Batch 10 - Moderation
    commands.registerExternalCommand("/modlogs", &modlogs);
    commands.registerExternalCommand("/acknowledgewarning", &acknowledgeWarning);

    // Batch N3 - Nuke (moderator emergency tool)
    commands.registerExternalCommand("/cancelnuke", &cancelNuke);

    // Batch N4 - Nuke undo
    commands.registerExternalCommand("/unnuke", &unnuke);

    // Batch 11 - Chat utils
    commands.registerExternalCommand("/resub", &resubNotification);
    commands.registerExternalCommand("/cheer", &cheer);
    commands.registerExternalCommand("/displayname", &displayName);
    commands.registerExternalCommand("/logsextended", &logsExtended);
}

}  // namespace chatterino::LimerinoCommands
