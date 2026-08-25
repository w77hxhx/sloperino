// SPDX-License-Identifier: MIT
// Batch 1 (Identity & account): /uid, /namehistory, /modlist ports plus the
// usercard "Name history" entry. Bodies transcribed from
// pluginforreference/init.lua + requests.lua.

#pragma once

#include <QString>

class QWidget;

namespace chatterino {

struct CommandContext;

namespace LimerinoCommands {

QString uid(const CommandContext &ctx);
QString nameHistory(const CommandContext &ctx);
QString modList(const CommandContext &ctx);

// Usercard integration (UserInfoPopup hook).
void showNameHistoryDialog(const QString &login, QWidget *parent);

}  // namespace LimerinoCommands
}  // namespace chatterino
