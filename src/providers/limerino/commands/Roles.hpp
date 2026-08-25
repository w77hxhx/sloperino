// SPDX-License-Identifier: MIT
// Batch 9 (Roles & editors): /artist, /unartist, /leadmod + usercard buttons
// (own channel only), and avatar-menu "editors / editor-in-channels" info
// widgets linking to https://7tv.app/users/<id>.
// GQL ops transcribed from pluginforreference/requests.lua.

#pragma once

#include <QString>

#include <memory>

namespace chatterino {

struct CommandContext;
class Channel;
using ChannelPtr = std::shared_ptr<Channel>;

namespace LimerinoCommands {

// Slash commands (plugin parity)
QString grantArtistCmd(const CommandContext &ctx);
QString revokeArtistCmd(const CommandContext &ctx);
QString grantLeadModCmd(const CommandContext &ctx);

// Usercard buttons (own channel only; visibility is decided by the popup)
void grantArtist(const QString &userLogin, const ChannelPtr &channel);
void revokeArtist(const QString &userLogin, const ChannelPtr &channel);
void grantLeadMod(const QString &userLogin, const ChannelPtr &channel);

// Avatar-menu entries (7tv lookups, any channel/user)
/** Editors of the given Twitch user (usercard target), not the split channel. */
void showSeventvUserEditors(const QString &twitchUserId,
                            const QString &userLogin,
                            const ChannelPtr &feedbackChannel);
void showSeventvUserEditorIn(const QString &userLogin);

}  // namespace LimerinoCommands
}  // namespace chatterino
