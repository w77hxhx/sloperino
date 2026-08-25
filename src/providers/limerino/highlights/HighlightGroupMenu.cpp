// SPDX-License-Identifier: MIT

#include "providers/limerino/highlights/HighlightGroupMenu.hpp"

#include "common/Channel.hpp"
#include "providers/limerino/highlights/HighlightGroup.hpp"
#include "providers/limerino/highlights/HighlightGroupChannelKey.hpp"
#include "providers/limerino/highlights/HighlightGroupDialog.hpp"
#include "singletons/Settings.hpp"

#include <QMenu>

namespace chatterino::limerino {

void buildHighlightGroupsMenuEntry(QMenu *menu, const Channel &channel,
                                   QWidget *parentForDialog)
{
    if (!menu)
    {
        return;
    }

    const auto type = channel.getType();
    if (type != Channel::Type::Twitch && type != Channel::Type::Kick &&
        type != Channel::Type::TwitchWatching)
    {
        return;  // not a real chat channel; no channel key to scope against
    }

    const auto key = highlightChannelKey(channel);

    QStringList names;
    auto groups = getSettings()->highlightGroups.readOnly();
    for (const auto &group : *groups)
    {
        if (group.matches(key))
        {
            names.append(group.displayName());
        }
    }

    auto *submenu = menu->addMenu(QStringLiteral("Highlight groups"));
    if (!submenu)
    {
        return;
    }

    if (names.isEmpty())
    {
        auto *emptyAction =
            submenu->addAction(QStringLiteral("No highlight groups active"));
        emptyAction->setEnabled(false);
        emptyAction->setToolTip(QStringLiteral(
            "No highlight group matches this channel. Only built-in global "
            "checks (self highlight, whispers, etc.) apply."));
    }
    else
    {
        for (const auto &name : names)
        {
            auto *action = submenu->addAction(name);
            action->setEnabled(false);
            action->setToolTip(QStringLiteral(
                "Read-only. Manage groups under Settings > Highlights."));
        }
    }

    submenu->addSeparator();
    QAction *manage =
        submenu->addAction(QStringLiteral("Manage groups…"));
    QAction::connect(manage, &QAction::triggered, parentForDialog,
                     [parentForDialog] {
                         HighlightGroupDialog dialog(parentForDialog);
                         dialog.exec();
                     });
}

}  // namespace chatterino::limerino
