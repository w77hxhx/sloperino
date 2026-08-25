// SPDX-License-Identifier: MIT
// Limerino theme creator: pure generator seed -> QJsonObject.
//
// THE PURE-FUNCTION RULE: nothing in this file touches Settings, Application,
// getTheme(), or any other global. Everything the theme contains is a function
// of the four seed colors. This is what makes it unit-testable.
//
// Derivation strategy (evidence in resources/themes/{Dark,Black,Light,White}.json):
// both built-in pairs differ from each other ONLY along the background/surface
// shading axis - accent/text/selection/indicators are constants or simple
// alpha overlays. Each leaf below names where it comes from in one comment.

#include "providers/limerino/theme/LimerinoThemeGenerator.hpp"

#include "providers/limerino/theme/LimerinoThemeSeed.hpp"

#include <QFile>
#include <QHash>
#include <QJsonArray>
#include <QJsonDocument>
#include <QJsonValue>

namespace chatterino::limerino {

namespace {

// -- helpers -----------------------------------------------------------------

/// Serialize like the built-ins: #AARRGGBB when alpha carries information,
/// else #RRGGBB.
QString encodeColor(const QColor &c)
{
    return c.alpha() < 255 ? c.name(QColor::HexArgb) : c.name(QColor::HexRgb);
}

/// Shift `from` `factor` of the way toward `toward` in HSL lightness.
/// (0..1; hu＆saturation preserved). Used to produce shade series.
QColor shadeToward(const QColor &from, const QColor &toward, double factor)
{
    return QColor::fromHslF(from.hueF(), from.saturationF(),
                            from.lightnessF() +
                                (toward.lightnessF() - from.lightnessF()) *
                                    factor);
}

/// `color` with the given alpha applied.
QColor withAlpha(const QColor &color, int alpha)
{
    QColor c = color;
    c.setAlpha(alpha);
    return c;
}

// -- per-leaf derivation -----------------------------------------------------

QJsonObject buildMetadata(const LimerinoThemeSeed &s)
{
    // Light themes need dark icons (and vice versa).
    return QJsonObject{
        {QStringLiteral("iconTheme"),
         s.isLight() ? QStringLiteral("dark") : QStringLiteral("light")},
    };
}

QJsonObject buildWindow(const LimerinoThemeSeed &s)
{
    return QJsonObject{
        // window surface = base surface, verbatim
        {QStringLiteral("background"), encodeColor(s.background)},
        // primary text, verbatim
        {QStringLiteral("text"), encodeColor(s.text)},
    };
}

QJsonObject buildMessages(const LimerinoThemeSeed &s)
{
    return QJsonObject{
        {QStringLiteral("backgrounds"),
         QJsonObject{
             // message rows sit on the base surface, verbatim
             {QStringLiteral("regular"), encodeColor(s.background)},
             // row banding: 50% from background toward text
             {QStringLiteral("alternate"),
              encodeColor(shadeToward(s.background, s.text, 0.5))},
         }},
        // faded rows: text 60% toward its own background
        {QStringLiteral("disabled"),
         encodeColor(shadeToward(s.text, s.background, 0.6))},
        // highlight flash animation: text-derived color at 0x43 alpha
        // (both built-ins use a pale overlay at roughly this alpha)
        {QStringLiteral("highlightAnimationEnd"),
         encodeColor(withAlpha(s.text, 0x6e))},
        {QStringLiteral("highlightAnimationStart"),
         encodeColor(withAlpha(s.text, 0x23))},
        // selection: text color at 0x40 alpha, identical in all 4 built-ins
        {QStringLiteral("selection"),
         encodeColor(withAlpha(s.text, 0x40))},
        {QStringLiteral("textColors"),
         QJsonObject{
             // caret accent cue
             {QStringLiteral("caret"), encodeColor(s.accent)},
             // placeholder placeholder: 70% toward background
             {QStringLiteral("chatPlaceholder"),
              encodeColor(shadeToward(s.text, s.background, 0.7))},
             // links pick up the theme accent
             {QStringLiteral("link"), encodeColor(s.accent)},
             // verbatim
             {QStringLiteral("regular"), encodeColor(s.text)},
             // system notices: 60% toward background
             {QStringLiteral("system"),
              encodeColor(shadeToward(s.text, s.background, 0.6))},
         }},
    };
}

// Overlay messages: all four built-ins keep a dark overlay look regardless of
// theme, so the overlay leaves dominate by constants.
QJsonObject buildOverlayMessages(const LimerinoThemeSeed &s)
{
    return QJsonObject{
        {QStringLiteral("backgrounds"),
         QJsonObject{
             // built-ins: "20-alpha of color-against-dark"; here of text
             {QStringLiteral("alternate"),
              encodeColor(withAlpha(s.text, 0x20))},
             // the built-ins literally write "transparent" - preserve it
             {QStringLiteral("regular"), QStringLiteral("transparent")},
         }},
        // constant across all 4 built-ins
        {QStringLiteral("disabled"), QStringLiteral("#64000000")},
        // constant across all 4 built-ins
        {QStringLiteral("selection"), QStringLiteral("#40ffffff")},
        {QStringLiteral("textColors"),
         QJsonObject{
             {QStringLiteral("caret"), QStringLiteral("#ffffff")},
             {QStringLiteral("chatPlaceholder"), QStringLiteral("#5d5555")},
             {QStringLiteral("link"), encodeColor(s.accent)},
             {QStringLiteral("regular"), QStringLiteral("#ffffff")},
             {QStringLiteral("system"), QStringLiteral("#8c7f7f")},
         }},
        // dark-family built-ins: #000; light-family: #333
        {QStringLiteral("background"),
         s.isLight() ? QStringLiteral("#333333") : QStringLiteral("#000000")},
    };
}

QJsonObject buildScrollbars(const LimerinoThemeSeed &s)
{
    return QJsonObject{
        // all 4 built-ins: fully transparent track
        {QStringLiteral("background"), encodeColor(QColor(0, 0, 0, 0))},
        // thumb floats a bit off the surface toward text
        {QStringLiteral("thumb"),
         encodeColor(shadeToward(s.surface, s.text, 0.35))},
        // selected thumb a bit further
        {QStringLiteral("thumbSelected"),
         encodeColor(shadeToward(s.surface, s.text, 0.5))},
    };
}

/// One tab-state block. backgrounds: surface series; line: caller's accent
/// (or its shaded variant); text: caller picks verbatim or the damped form.
QJsonObject buildTabColors(const LimerinoThemeSeed &s, const QColor &line,
                           bool selected)
{
    return QJsonObject{
        {QStringLiteral("backgrounds"),
         QJsonObject{
             {QStringLiteral("regular"), encodeColor(s.surface)},
             {QStringLiteral("hover"),
              encodeColor(shadeToward(s.surface, s.text, 0.1))},
             {QStringLiteral("unfocused"),
              encodeColor(shadeToward(s.surface, s.background, 0.5))},
         }},
        {QStringLiteral("line"),
         QJsonObject{
             {QStringLiteral("regular"), encodeColor(line)},
             {QStringLiteral("hover"), encodeColor(line)},
             {QStringLiteral("unfocused"), encodeColor(line)},
         }},
        {QStringLiteral("text"),
         encodeColor(selected ? s.text
                              : shadeToward(s.text, s.background, 0.35))},
    };
}

QJsonObject buildTabs(const LimerinoThemeSeed &s)
{
    return QJsonObject{
        // skin between tabs: 40% toward text from surface
        {QStringLiteral("dividerLine"),
         encodeColor(shadeToward(s.surface, s.text, 0.4))},
        // built-ins never move these indicators; keep both constants.
        {QStringLiteral("liveIndicator"), QStringLiteral("#ff0000")},
        {QStringLiteral("rerunIndicator"), QStringLiteral("#c7c715")},
        {QStringLiteral("regular"),
         buildTabColors(s, shadeToward(s.surface, s.text, 0.3),
                        /*selected=*/false)},
        {QStringLiteral("newMessage"),
         buildTabColors(s, s.accent, /*selected=*/true)},
        {QStringLiteral("highlighted"),
         // built-ins: highlighted tabs get a red line
         buildTabColors(s, QColor(QStringLiteral("#ee6166")),
                        /*selected=*/true)},
        {QStringLiteral("selected"),
         buildTabColors(s, s.accent, /*selected=*/true)},
    };
}

QJsonObject buildSplits(const LimerinoThemeSeed &s)
{
    return QJsonObject{
        // base surface, verbatim
        {QStringLiteral("background"), encodeColor(s.background)},
        // splits drop preview: accent at 0x30 alpha, matching both built-ins
        {QStringLiteral("dropPreview"),
         encodeColor(withAlpha(s.accent, 0x30))},
        // drop outline: solid accent
        {QStringLiteral("dropPreviewBorder"), encodeColor(s.accent)},
        // fill of the drop target rect: accent, nearly invisible
        {QStringLiteral("dropTargetRect"),
         encodeColor(withAlpha(s.accent, 0x00))},
        {QStringLiteral("dropTargetRectBorder"), encodeColor(s.accent)},
        // resize handle: accent at 0x70 (hover bg of splits) with 0x20 track
        {QStringLiteral("resizeHandle"),
         encodeColor(withAlpha(s.accent, 0x70))},
        {QStringLiteral("resizeHandleBackground"),
         encodeColor(withAlpha(s.accent, 0x20))},
        // horizontal separators between splits
        {QStringLiteral("messageSeperator"),
         encodeColor(shadeToward(s.background, s.text, 0.4))},
        {QStringLiteral("header"),
         QJsonObject{
             // split header / window title bar = secondary surface
             {QStringLiteral("background"), encodeColor(s.surface)},
             {QStringLiteral("border"),
              encodeColor(shadeToward(s.surface, s.text, 0.1))},
             {QStringLiteral("focusedBackground"),
              encodeColor(shadeToward(s.surface, s.text, 0.25))},
             {QStringLiteral("focusedBorder"),
              encodeColor(shadeToward(s.surface, s.text, 0.3))},
             // normal split titles: slightly damped text
             {QStringLiteral("text"),
              encodeColor(shadeToward(s.text, s.background, 0.15))},
             // focused tab title: accent
             {QStringLiteral("focusedText"), encodeColor(s.accent)},
         }},
        {QStringLiteral("input"),
         QJsonObject{
             // input area: one step off the surface toward the background
             {QStringLiteral("background"),
              encodeColor(shadeToward(s.surface, s.background, 0.3))},
             // "participant wrote something" pulse: built-ins use green
             {QStringLiteral("backgroundPulse"),
              QStringLiteral("#215421")},
             // constant red across built-ins
             {QStringLiteral("searchFailText"),
              QStringLiteral("#ff0000")},
             // find-highlight: text toward accent at half blend
             {QStringLiteral("searchHighlightBackground"),
              encodeColor(shadeToward(s.text, s.accent, 0.5))},
             // verbatim
             {QStringLiteral("text"), encodeColor(s.text)},
         }},
    };
}

QJsonObject synthesizeTheme(const LimerinoThemeSeed &seed)
{
    QJsonObject root;
    // Canonical public URL form: exports validate in editors outside the
    // source tree (the bundled ../..-relative path is meaningless there).
    root.insert(QStringLiteral("$schema"),
                QStringLiteral("https://raw.githubusercontent.com/lagx/"
                               "Limerino/limerino/docs/"
                               "ChatterinoTheme.schema.json"));
    root.insert(QStringLiteral("metadata"), buildMetadata(seed));

    QJsonObject colors;
    colors.insert(QStringLiteral("accent"), encodeColor(seed.accent));
    colors.insert(QStringLiteral("window"), buildWindow(seed));
    colors.insert(QStringLiteral("tabs"), buildTabs(seed));
    colors.insert(QStringLiteral("messages"), buildMessages(seed));
    colors.insert(QStringLiteral("overlayMessages"),
                  buildOverlayMessages(seed));
    colors.insert(QStringLiteral("scrollbars"), buildScrollbars(seed));
    colors.insert(QStringLiteral("splits"), buildSplits(seed));
    root.insert(QStringLiteral("colors"), colors);
    return root;
}

QColor parseThemeColor(const QString &raw, const QColor &fallback)
{
    if (raw.isEmpty() ||
        raw.compare(QStringLiteral("transparent"), Qt::CaseInsensitive) == 0)
    {
        return fallback;
    }
    const QColor c(raw);
    return c.isValid() ? c : fallback;
}

QString rgbKey(const QColor &c)
{
    return c.name(QColor::HexRgb).toLower();
}

QString remapColorString(const QString &raw,
                         const QHash<QString, QColor> &map)
{
    if (raw.compare(QStringLiteral("transparent"), Qt::CaseInsensitive) == 0)
    {
        return raw;
    }
    const QColor c(raw);
    if (!c.isValid())
    {
        return raw;
    }
    const auto it = map.constFind(rgbKey(c));
    if (it == map.cend())
    {
        return raw;
    }
    QColor next = it.value();
    next.setAlpha(c.alpha());
    return encodeColor(next);
}

QJsonValue remapValue(const QJsonValue &v, const QHash<QString, QColor> &map)
{
    if (v.isString())
    {
        return remapColorString(v.toString(), map);
    }
    if (v.isObject())
    {
        const QJsonObject in = v.toObject();
        QJsonObject out;
        for (auto it = in.begin(); it != in.end(); ++it)
        {
            out.insert(it.key(), remapValue(it.value(), map));
        }
        return out;
    }
    if (v.isArray())
    {
        const QJsonArray in = v.toArray();
        QJsonArray out;
        for (int i = 0; i < in.size(); ++i)
        {
            out.append(remapValue(in.at(i), map));
        }
        return out;
    }
    return v;
}

constexpr const char *PUBLIC_SCHEMA =
    "https://raw.githubusercontent.com/lagx/Limerino/limerino/docs/"
    "ChatterinoTheme.schema.json";

void stampSchemaAndIconTheme(QJsonObject &root, const LimerinoThemeSeed &seed)
{
    root.insert(QStringLiteral("$schema"), QString::fromUtf8(PUBLIC_SCHEMA));
    QJsonObject meta = root.value(QStringLiteral("metadata")).toObject();
    meta.insert(QStringLiteral("iconTheme"),
                seed.isLight() ? QStringLiteral("dark")
                               : QStringLiteral("light"));
    root.insert(QStringLiteral("metadata"), meta);
}

}  // namespace

QString builtinThemeName(BuiltinTheme builtin)
{
    switch (builtin)
    {
        case BuiltinTheme::Light:
            return QStringLiteral("Light");
        case BuiltinTheme::Black:
            return QStringLiteral("Black");
        case BuiltinTheme::White:
            return QStringLiteral("White");
        case BuiltinTheme::Dark:
        default:
            return QStringLiteral("Dark");
    }
}

std::optional<QJsonObject> loadBuiltinThemeJson(BuiltinTheme builtin)
{
    QFile file(QStringLiteral(":/themes/%1.json").arg(builtinThemeName(builtin)));
    if (!file.open(QIODevice::ReadOnly | QIODevice::Text))
    {
        return std::nullopt;
    }
    const auto doc = QJsonDocument::fromJson(file.readAll());
    if (!doc.isObject() || doc.object().isEmpty())
    {
        return std::nullopt;
    }
    return doc.object();
}

LimerinoThemeSeed seedFromThemeJson(const QJsonObject &theme)
{
    const LimerinoThemeSeed fallback = LimerinoThemeSeed::darkPreset();
    const QJsonObject colors = theme.value(QStringLiteral("colors")).toObject();
    const QJsonObject messages =
        colors.value(QStringLiteral("messages")).toObject();
    const QJsonObject splits =
        colors.value(QStringLiteral("splits")).toObject();
    const QJsonObject window =
        colors.value(QStringLiteral("window")).toObject();
    const QJsonObject header =
        splits.value(QStringLiteral("header")).toObject();
    const QJsonObject msgBg =
        messages.value(QStringLiteral("backgrounds")).toObject();
    const QJsonObject msgText =
        messages.value(QStringLiteral("textColors")).toObject();

    LimerinoThemeSeed seed = fallback;
    seed.accent = parseThemeColor(
        colors.value(QStringLiteral("accent")).toString(), fallback.accent);

    const QString bgRaw = msgBg.value(QStringLiteral("regular")).toString();
    seed.background = parseThemeColor(
        bgRaw.isEmpty() ? window.value(QStringLiteral("background")).toString()
                        : bgRaw,
        fallback.background);
    if (!seed.background.isValid())
    {
        seed.background = parseThemeColor(
            splits.value(QStringLiteral("background")).toString(),
            fallback.background);
    }

    seed.surface = parseThemeColor(
        header.value(QStringLiteral("background")).toString(),
        fallback.surface);

    const QString textRaw = msgText.value(QStringLiteral("regular")).toString();
    seed.text = parseThemeColor(
        textRaw.isEmpty() ? window.value(QStringLiteral("text")).toString()
                          : textRaw,
        fallback.text);
    return seed;
}

QJsonObject recolorTheme(const QJsonObject &base, const LimerinoThemeSeed &from,
                         const LimerinoThemeSeed &to)
{
    QHash<QString, QColor> map;
    // Later inserts win on RGB collisions (text over accent over surface).
    map.insert(rgbKey(from.background), to.background);
    map.insert(rgbKey(from.surface), to.surface);
    map.insert(rgbKey(from.accent), to.accent);
    map.insert(rgbKey(from.text), to.text);
    return remapValue(base, map).toObject();
}

QJsonObject generateThemeFromBase(const QJsonObject &base,
                                  const LimerinoThemeSeed &baseSeed,
                                  const LimerinoThemeSeed &seed)
{
    QJsonObject out = recolorTheme(base, baseSeed, seed);
    stampSchemaAndIconTheme(out, seed);
    return out;
}

QJsonObject generateTheme(const LimerinoThemeSeed &seed)
{
    const BuiltinTheme builtin =
        seed.isLight() ? BuiltinTheme::Light : BuiltinTheme::Dark;
    const auto base = loadBuiltinThemeJson(builtin);
    if (!base.has_value())
    {
        return synthesizeTheme(seed);
    }
    return generateThemeFromBase(*base, seedFromThemeJson(*base), seed);
}

QStringList contrastWarnings(const LimerinoThemeSeed &seed)
{
    QStringList warnings;
    auto check = [&](const QString &what, const QColor &fg,
                     const QColor &bg) {
        const double ratio = contrastRatio(fg, bg);
        // WCAG large-text minimum; the pairs below are the ones users read.
        if (ratio < 3.0)
        {
            warnings.append(QStringLiteral("%1 (contrast %2)")
                                .arg(what)
                                .arg(ratio, 0, 'f', 2));
        }
    };
    check(QStringLiteral("primary text on background"), seed.text,
          seed.background);
    check(QStringLiteral("text on surface"), seed.text, seed.surface);
    check(QStringLiteral("accent on background"), seed.accent,
          seed.background);
    check(QStringLiteral("accent on surface"), seed.accent, seed.surface);
    return warnings;
}

}  // namespace chatterino::limerino
