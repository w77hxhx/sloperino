// SPDX-License-Identifier: MIT
// Read-only "Highlight groups" entry for the split context menu (H4).
// Lists the groups whose scope makes them active in the split's channel.
// The entry is informational only; no state is written.
// A trailing "Manage groups…" action opens the full dialog (H3).

#pragma once

class QMenu;
class QWidget;

namespace chatterino {

class Channel;

namespace limerino {

/// Appends a "Highlight groups" submenu to @a menu, positioned wherever the
/// caller invokes this. Does nothing for non-chat channels or when there are
/// no groups.
///
/// @param menu The menu to append to.
/// @param channel The channel whose key is resolved; must be a real
///                Twitch/Kick chat channel.
/// @param parentForDialog Widget used as the dialog parent when the user
///                        picks "Manage groups…".
void buildHighlightGroupsMenuEntry(QMenu *menu, const Channel &channel,
                                   QWidget *parentForDialog);

}  // namespace limerino
}  // namespace chatterino
