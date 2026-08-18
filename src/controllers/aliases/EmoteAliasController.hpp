// SPDX-FileCopyrightText: 2026 Contributors to Chatterino <https://chatterino.com>
//
// SPDX-License-Identifier: MIT

#pragma once

#include "messages/Emote.hpp"

#include <QString>

#include <memory>
#include <optional>

namespace chatterino {

class EmoteAliasController
{
public:
    static std::optional<EmotePtr> findAliasEmote(const QString &word);
    static void clearCache();
};

}  // namespace chatterino
