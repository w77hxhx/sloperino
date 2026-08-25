// SPDX-License-Identifier: MIT

#include "providers/limerino/highlights/HighlightGroupChannels.hpp"

#include "Application.hpp"
#include "common/Channel.hpp"
#include "providers/limerino/highlights/HighlightGroupChannelKey.hpp"
#include "singletons/Settings.hpp"
#include "singletons/WindowManager.hpp"
#include "widgets/Notebook.hpp"
#include "widgets/splits/Split.hpp"
#include "widgets/Window.hpp"

namespace chatterino::limerino {

QSet<QString> knownHighlightChannelKeys()
{
    QSet<QString> result;

    // 1) Every split in every window.
    auto *wm = getApp() ? getApp()->getWindows() : nullptr;
    if (wm)
    {
        for (auto *window : wm->windows())
        {
            if (!window)
            {
                continue;
            }
            window->getNotebook().forEachSplit([&result](Split *split) {
                if (!split)
                {
                    return;
                }
                auto channel = split->getChannel();
                if (!channel)
                {
                    return;
                }
                result.insert(highlightChannelKey(*channel));
            });
        }
    }

    // 2) Persisted channel entries from existing groups: keep showing them in
    //    the completion list so old entries are not silently dropped.
    auto *settings = getSettings();
    if (settings)
    {
        auto groups = settings->highlightGroups.readOnly();
        for (const auto &group : *groups)
        {
            for (const auto &channel : group.channels())
            {
                result.insert(channel);
            }
        }
    }

    return result;
}

}  // namespace chatterino::limerino
