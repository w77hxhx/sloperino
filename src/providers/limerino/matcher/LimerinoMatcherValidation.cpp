// SPDX-License-Identifier: MIT

#include "providers/limerino/matcher/LimerinoMatcherValidation.hpp"

#include "providers/limerino/matcher/LimerinoMatcher.hpp"

namespace chatterino::limerino {

std::optional<QString> validateMatcherPair(
    const LimerinoMatcher &contentMatcher, const LimerinoMatcher &senderMatcher,
    const QString &fieldContentName, const QString &fieldSenderName)
{
    if (contentMatcher.isEmpty() && senderMatcher.isEmpty())
    {
        return QStringLiteral(
            "Both matchers are empty: this would select every message in the "
            "channel. Provide at least one of %1 or %2.")
            .arg(fieldContentName, fieldSenderName);
    }

    if (const auto err = contentMatcher.compileError())
    {
        return QStringLiteral("%1 matcher: invalid regex — %2")
            .arg(fieldContentName, *err);
    }
    if (const auto err = senderMatcher.compileError())
    {
        return QStringLiteral("%1 matcher: invalid regex — %2")
            .arg(fieldSenderName, *err);
    }

    return std::nullopt;
}

}  // namespace chatterino::limerino
