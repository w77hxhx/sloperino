// SPDX-FileCopyrightText: 2026 Contributors to Chatterino <https://chatterino.com>
//
// SPDX-License-Identifier: MIT

#pragma once

#include <QColor>

namespace chatterino::semantic {

/// Theme-aware semantic colors shared by fork feature widgets.
///
/// Fork-specific dialogs and settings pages used to hardcode colors that
/// assumed a dark background, making them stand out from the rest of
/// Chatterino. Use these helpers instead so the widgets follow the active
/// theme like everything else.

/// Primary text color (follows the message text color of the theme).
QColor regularText();

/// Secondary/muted text for descriptions and detail rows.
QColor mutedText();

/// Positive state (connected, valid, saved).
QColor success();

/// Negative state (errors, failed validation).
QColor error();

/// Caution state (reconnecting, pending, irreversible actions).
QColor warning();

}  // namespace chatterino::semantic
