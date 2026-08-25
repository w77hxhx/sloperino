// SPDX-License-Identifier: MIT
// Chat-colour helpers for Appearance (E7): Helix named colours, hex parse,
// and a persisted MRU of custom colours (not ColorProvider highlight recents).

#pragma once

#include <QColor>
#include <QString>
#include <QStringList>

namespace chatterino::limerino {

constexpr int kChatColorRecentsLimit = 5;

/// Resolve a Helix colour name (`blue_violet`) or hex (`#RGB`/`#RRGGBB`/
/// `#AARRGGBB`) to a QColor. Invalid input yields an invalid QColor.
QColor parseChatColor(const QString &value);

/// True when `value` is one of VALID_HELIX_COLORS (after cleanHelixColorName).
bool isHelixNamedColor(const QString &value);

/// Canonical storage / Helix form: lowercased helix name, or #RRGGBB /
/// #AARRGGBB for custom colours. Empty if unparseable.
QString normalizeChatColorValue(const QString &value);

/// Display hex for a resolved colour (ARGB when alpha < 255).
QString chatColorHexForDisplay(const QColor &color);

/// MRU insert: move `value` to front, dedupe, cap at `limit`. Returns the
/// new list. Only meaningful for custom (non-Helix-name) values in the UI,
/// but the helper itself does not filter.
QStringList pushChatColorRecent(QStringList existing, const QString &value,
                                int limit = kChatColorRecentsLimit);

QStringList loadChatColorRecents();
void saveChatColorRecents(const QStringList &recents);

/// Last selected Appearance colour (helix name or hex). Empty if unset.
QString loadLastChatColor();
void saveLastChatColor(const QString &value);

}  // namespace chatterino::limerino
