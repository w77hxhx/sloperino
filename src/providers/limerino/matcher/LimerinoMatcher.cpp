// SPDX-License-Identifier: MIT

#include "providers/limerino/matcher/LimerinoMatcher.hpp"

namespace chatterino::limerino {

LimerinoMatcher::LimerinoMatcher(QString pattern, bool isCaseSensitive,
                                 bool isRegex)
    : pattern(std::move(pattern))
    , caseSensitive(isCaseSensitive)
    , isRegex(isRegex)
{
    this->rebuild();
}

void LimerinoMatcher::rebuild()
{
    if (this->isRegex)
    {
        this->compiled = QRegularExpression(
            this->pattern,
            this->caseSensitive
                ? QRegularExpression::NoPatternOption
                : QRegularExpression::CaseInsensitiveOption);
        this->literalNeedle.clear();
    }
    else
    {
        this->compiled = QRegularExpression();
        this->literalNeedle = this->caseSensitive ? this->pattern
                                                  : this->pattern.toLower();
    }
}

bool LimerinoMatcher::matches(QStringView text) const
{
    if (this->pattern.isEmpty())
    {
        return true;  // absent constraint
    }

    if (this->isRegex)
    {
        if (!this->compiled.isValid())
        {
            return false;  // invalid regex: no fallback to substring
        }
        return this->compiled.matchView(text).hasMatch();
    }

    if (this->caseSensitive)
    {
        return text.contains(this->literalNeedle);
    }
    const auto lowered = text.toString().toLower();
    return lowered.contains(this->literalNeedle);
}

void LimerinoMatcher::normalize()
{
    this->rebuild();
}

std::optional<QString> LimerinoMatcher::compileError() const
{
    if (!this->isRegex || this->compiled.isValid())
    {
        return std::nullopt;
    }
    return this->compiled.errorString();
}

}  // namespace chatterino::limerino

namespace pajlada {

rapidjson::Value Serialize<chatterino::limerino::LimerinoMatcher>::get(
    const chatterino::limerino::LimerinoMatcher &value,
    rapidjson::Document::AllocatorType &a)
{
    rapidjson::Value ret(rapidjson::kObjectType);

    chatterino::rj::set(ret, "pattern", value.pattern, a);
    chatterino::rj::set(ret, "case", value.caseSensitive, a);
    chatterino::rj::set(ret, "regex", value.isRegex, a);

    return ret;
}

chatterino::limerino::LimerinoMatcher
    Deserialize<chatterino::limerino::LimerinoMatcher>::get(
        const rapidjson::Value &value, bool *error)
{
    if (!value.IsObject())
    {
        PAJLADA_REPORT_ERROR(error)
        return {};
    }

    QString pattern;
    bool caseSensitive = false;
    bool isRegex = true;

    chatterino::rj::getSafe(value, "pattern", pattern);
    chatterino::rj::getSafe(value, "case", caseSensitive);
    chatterino::rj::getSafe(value, "regex", isRegex);

    // Tolerant: an invalid regex round-trips and re-surfaces via
    // compileError() rather than being dropped or rewritten here.
    return {pattern, caseSensitive, isRegex};
}

}  // namespace pajlada
