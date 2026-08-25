#include "providers/limerino/matcher/LimerinoMatcher.hpp"
#include "providers/limerino/matcher/LimerinoMatcherValidation.hpp"

#include <gtest/gtest.h>

#include <QString>

using namespace chatterino;
using chatterino::limerino::LimerinoMatcher;
using chatterino::limerino::validateMatcherPair;

namespace {

LimerinoMatcher regex(QString pattern, bool caseSensitive = false)
{
    return {std::move(pattern), caseSensitive, /*isRegex=*/true};
}

LimerinoMatcher literal(QString pattern, bool caseSensitive = false)
{
    return {std::move(pattern), caseSensitive, /*isRegex=*/false};
}

}  // namespace

// --- single-matcher semantics ------------------------------------------------

TEST(LimerinoMatcher, EmptyMatchesEverything)
{
    LimerinoMatcher m;  // default: pattern="", regex, case-insensitive

    EXPECT_TRUE(m.matches(QStringView()));
    EXPECT_TRUE(m.matches(u"anything at all"));
    EXPECT_TRUE(m.matches(u"Иди сюда"));
}

TEST(LimerinoMatcher, RegexCaseInsensitiveByDefault)
{
    auto m = regex(QStringLiteral("bad"));

    EXPECT_TRUE(m.matches(u"You BAD person"));
    EXPECT_TRUE(m.matches(u"bad"));
    EXPECT_FALSE(m.matches(u"goad"));  // no match of the literal word
}

TEST(LimerinoMatcher, RegexCaseSensitiveToggle)
{
    auto m = regex(QStringLiteral("Bad"), /*caseSensitive=*/true);

    EXPECT_TRUE(m.matches(u"You Bad person"));
    EXPECT_FALSE(m.matches(u"you bad person"));
}

TEST(LimerinoMatcher, RegexAnchorsAffectNothingWhenNonEmpty)
{
    auto m = regex(QStringLiteral("^https?://"));

    EXPECT_TRUE(m.matches(u"https://example.com"));
    EXPECT_FALSE(m.matches(u"see https://example.com"));
}

TEST(LimerinoMatcher, LiteralIsSubstringNotAnchored)
{
    auto m = literal(QStringLiteral("qweb"));

    EXPECT_TRUE(m.matches(u"one qweb two"));
    EXPECT_TRUE(m.matches(u"qweb"));
    EXPECT_FALSE(m.matches(u"qwe"));
}

TEST(LimerinoMatcher, LiteralCaseInsensitiveByDefault)
{
    auto m = literal(QStringLiteral("SPAM"));

    EXPECT_TRUE(m.matches(u"stop spamming"));
    EXPECT_TRUE(m.matches(u"spam"));
}

TEST(LimerinoMatcher, LiteralCaseSensitiveToggle)
{
    auto m = literal(QStringLiteral("SPAM"), /*caseSensitive=*/true);

    EXPECT_TRUE(m.matches(u"stop SPAMming"));
    EXPECT_FALSE(m.matches(u"stop spamming"));
}

TEST(LimerinoMatcher, LiteralModeDoesNotInterpretRegexSyntax)
{
    // A pattern that would be a malformed regex but is a legal plain string.
    auto m = literal(QStringLiteral("C++ (test)"));

    EXPECT_TRUE(m.matches(u"Writing C++ (test) is fun"));
    EXPECT_FALSE(m.compileError().has_value());
}

// --- invalid regex handling ---------------------------------------------------

TEST(LimerinoMatcher, InvalidRegexSurfacesAndNeverFallsBack)
{
    auto m = regex(QStringLiteral("(unclosed"));

    ASSERT_TRUE(m.compileError().has_value());
    EXPECT_FALSE(m.matches(u"(unclosed"));
    EXPECT_FALSE(m.matches(u"anything"));
}

TEST(LimerinoMatcher, InvalidRegexIsRejectedAsPairMember)
{
    auto bad = regex(QStringLiteral("(unclosed"));
    auto good = regex(QStringLiteral("spam"));

    // Either matcher invalid -> pair rejected, regardless of the other.
    EXPECT_TRUE(validateMatcherPair(bad, good).has_value());
    EXPECT_TRUE(validateMatcherPair(good, bad).has_value());
}

TEST(LimerinoMatcher, EmptyMatcherNeverCompileErrors)
{
    LimerinoMatcher empty;

    EXPECT_FALSE(empty.compileError().has_value());
}

// --- pair validation (the both-empty rejection lives here) --------------------

TEST(LimerinoMatcher, BothEmptyPairRejected)
{
    LimerinoMatcher empty;

    auto result = validateMatcherPair(empty, empty);

    ASSERT_TRUE(result.has_value());
    // Must mention the reason, not just "invalid".
    EXPECT_TRUE(result->contains(QStringLiteral("every message"),
                                 Qt::CaseInsensitive));
}

TEST(LimerinoMatcher, OneSidedPairsAccepted)
{
    LimerinoMatcher empty;

    EXPECT_FALSE(validateMatcherPair(regex(QStringLiteral("spam")), empty)
                     .has_value());
    EXPECT_FALSE(validateMatcherPair(empty, regex(QStringLiteral("spam")))
                     .has_value());
    EXPECT_FALSE(validateMatcherPair(literal(QStringLiteral("spam")), empty)
                     .has_value());
}

TEST(LimerinoMatcher, PairAcceptsValidRegardlessOfEmptyCombinations)
{
    auto a = regex(QStringLiteral("spam"));
    auto b = regex(QStringLiteral("badactor"));

    EXPECT_FALSE(validateMatcherPair(a, b).has_value());
}

// --- serde round-trip ----------------------------------------------------------

TEST(LimerinoMatcher, SerdeRoundTripPreservesAllFields)
{
    LimerinoMatcher original(QStringLiteral("(?:spam|phish)"),
                             /*caseSensitive=*/true, /*isRegex=*/true);

    rapidjson::Document doc;
    auto value = pajlada::Serialize<LimerinoMatcher>::get(original,
                                                          doc.GetAllocator());

    bool error = false;
    auto back = pajlada::Deserialize<LimerinoMatcher>::get(value, &error);

    EXPECT_FALSE(error);
    EXPECT_EQ(back.pattern, original.pattern);
    EXPECT_EQ(back.caseSensitive, original.caseSensitive);
    EXPECT_EQ(back.isRegex, original.isRegex);
    EXPECT_TRUE(back.matches(u"I SPAM here"));
    EXPECT_FALSE(back.matches(u"I spam here"));  // caseSensitive preserved
}

TEST(LimerinoMatcher, SerdeAbsentFieldsDefault)
{
    // An empty {} deserialises to pattern="", case-insensitive, regex on.
    rapidjson::Document doc;
    doc.Parse("{}");

    bool error = false;
    auto back = pajlada::Deserialize<LimerinoMatcher>::get(doc, &error);

    EXPECT_FALSE(error);
    EXPECT_TRUE(back.pattern.isEmpty());
    EXPECT_FALSE(back.caseSensitive);
    EXPECT_TRUE(back.isRegex);
    EXPECT_TRUE(back.matches(u"anything"));
}

TEST(LimerinoMatcher, SerdeNonObjectFailsCleanlyToEmptyMatcher)
{
    rapidjson::Document doc;
    doc.Parse("\"not an object\"");

    bool error = false;
    auto back = pajlada::Deserialize<LimerinoMatcher>::get(doc, &error);

    // PAJLADA_REPORT_ERROR sets *error; the fallback is an empty (absent)
    // matcher, never a crash.
    EXPECT_TRUE(error);
    EXPECT_TRUE(back.pattern.isEmpty());
    EXPECT_TRUE(back.matches(u"anything"));
    EXPECT_TRUE(validateMatcherPair(back, back).has_value());
}
