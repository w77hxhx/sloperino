// SPDX-FileCopyrightText: 2026 Contributors to Chatterino <https://chatterino.com>
//
// SPDX-License-Identifier: MIT

#include "controllers/aliases/EmoteAliasController.hpp"

#include "controllers/aliases/EmoteAlias.hpp"
#include "singletons/Settings.hpp"

#include <mutex>
#include <vector>

namespace chatterino {

namespace {

struct CachedAlias {
    QString word;
    QString link;
    bool isCaseSensitive;
    EmotePtr emote;
};

std::mutex s_mutex;
std::vector<CachedAlias> s_cachedAliases;
bool s_initialized = false;

void refreshCacheLocked()
{
    s_cachedAliases.clear();
    auto snapshot = getSettings()->customEmoteAliases.readOnly();
    for (const auto &alias : *snapshot)
    {
        if (alias.word().isEmpty() || alias.link().isEmpty())
        {
            continue;
        }
        auto emote = alias.createEmote();
        if (emote)
        {
            s_cachedAliases.push_back({
                alias.word(),
                alias.link(),
                alias.isCaseSensitive(),
                std::move(emote),
            });
        }
    }
}

}  // namespace

std::optional<EmotePtr> EmoteAliasController::findAliasEmote(
    const QString &word)
{
    if (word.isEmpty())
    {
        return std::nullopt;
    }

    std::lock_guard<std::mutex> lock(s_mutex);
    if (!s_initialized)
    {
        s_initialized = true;
        refreshCacheLocked();
        getSettings()->customEmoteAliases.delayedItemsChanged.connect([] {
            std::lock_guard<std::mutex> lk(s_mutex);
            refreshCacheLocked();
        });
    }

    for (const auto &cached : s_cachedAliases)
    {
        const auto cs =
            cached.isCaseSensitive ? Qt::CaseSensitive : Qt::CaseInsensitive;
        if (cached.word.compare(word, cs) == 0)
        {
            return cached.emote;
        }
    }

    return std::nullopt;
}

void EmoteAliasController::clearCache()
{
    std::lock_guard<std::mutex> lock(s_mutex);
    s_cachedAliases.clear();
    s_initialized = false;
}

}  // namespace chatterino
