// SPDX-FileCopyrightText: 2026 Contributors to Chatterino <https://chatterino.com>
//
// SPDX-License-Identifier: MIT

#include "util/SemanticColors.hpp"

#include "Application.hpp"
#include "singletons/Theme.hpp"

namespace chatterino::semantic {

namespace {

bool isLightTheme()
{
    auto *app = tryGetApp();
    if (app == nullptr)
    {
        return false;
    }
    return app->getThemes()->isLightTheme();
}

}  // namespace

QColor regularText()
{
    auto *app = tryGetApp();
    if (app == nullptr)
    {
        return QColor(210, 210, 210);
    }
    return app->getThemes()->messages.textColors.regular;
}

QColor mutedText()
{
    // Follows the theme's system-message color, which is tuned to be a
    // readable secondary tone on both light and dark backgrounds.
    auto *app = tryGetApp();
    if (app == nullptr)
    {
        return QColor(154, 160, 166);
    }
    return app->getThemes()->messages.textColors.system;
}

QColor success()
{
    return isLightTheme() ? QColor("#1a7f37") : QColor("#47d16c");
}

QColor error()
{
    return isLightTheme() ? QColor("#d1242f") : QColor("#ff7b72");
}

QColor warning()
{
    return isLightTheme() ? QColor("#b7791f") : QColor("#f0ad4e");
}

}  // namespace chatterino::semantic
