// SPDX-License-Identifier: MIT

#include "providers/limerino/appearance/LimerinoChatColor.hpp"

#include "singletons/Settings.hpp"
#include "util/RapidJsonSerializeQString.hpp"  // IWYU pragma: keep
#include "util/serialize/List.hpp"             // IWYU pragma: keep
#include "util/Twitch.hpp"

#include <pajlada/serialize.hpp>

#include <rapidjson/document.h>
#include <rapidjson/stringbuffer.h>
#include <rapidjson/writer.h>

namespace chatterino::limerino {

namespace {

QString cssNameFromHelix(QString helix)
{
    helix = helix.trimmed().toLower();
    helix.remove(QLatin1Char('_'));
    helix.remove(QLatin1Char(' '));
    return helix;
}

}  // namespace

QColor parseChatColor(const QString &value)
{
    const QString trimmed = value.trimmed();
    if (trimmed.isEmpty())
    {
        return {};
    }

    if (trimmed.startsWith(QLatin1Char('#')))
    {
        const QColor hex(trimmed);
        return hex.isValid() ? hex : QColor();
    }

    QString helix = trimmed;
    cleanHelixColorName(helix);
    if (isHelixNamedColor(helix))
    {
        const QColor named(cssNameFromHelix(helix));
        if (named.isValid())
        {
            return named;
        }
    }

    const QColor fallback(trimmed);
    return fallback.isValid() ? fallback : QColor();
}

bool isHelixNamedColor(const QString &value)
{
    QString helix = value.trimmed();
    cleanHelixColorName(helix);
    return VALID_HELIX_COLORS.contains(helix);
}

QString normalizeChatColorValue(const QString &value)
{
    const QString trimmed = value.trimmed();
    if (trimmed.isEmpty())
    {
        return {};
    }

    if (trimmed.startsWith(QLatin1Char('#')))
    {
        const QColor c(trimmed);
        if (!c.isValid())
        {
            return {};
        }
        return chatColorHexForDisplay(c);
    }

    QString helix = trimmed;
    cleanHelixColorName(helix);
    if (VALID_HELIX_COLORS.contains(helix))
    {
        return helix;
    }

    const QColor c(trimmed);
    if (c.isValid())
    {
        return chatColorHexForDisplay(c);
    }
    return {};
}

QString chatColorHexForDisplay(const QColor &color)
{
    if (!color.isValid())
    {
        return {};
    }
    return color.alpha() < 255 ? color.name(QColor::HexArgb)
                               : color.name(QColor::HexRgb);
}

QStringList pushChatColorRecent(QStringList existing, const QString &value,
                                int limit)
{
    const QString normalized = normalizeChatColorValue(value);
    if (normalized.isEmpty() || limit <= 0)
    {
        return existing;
    }

    existing.removeAll(normalized);
    // Also drop equivalent hex/name duplicates that normalize differently but
    // resolve to the same colour (compare by HexArgb).
    const QColor incoming = parseChatColor(normalized);
    if (incoming.isValid())
    {
        const QString key = incoming.name(QColor::HexArgb);
        QStringList filtered;
        filtered.reserve(existing.size());
        for (const auto &entry : existing)
        {
            const QColor other = parseChatColor(entry);
            if (other.isValid() &&
                other.name(QColor::HexArgb) == key)
            {
                continue;
            }
            filtered.append(entry);
        }
        existing = std::move(filtered);
    }

    existing.prepend(normalized);
    while (existing.size() > limit)
    {
        existing.removeLast();
    }
    return existing;
}

QStringList loadChatColorRecents()
{
    const auto raw = getSettings()->limerinoChatColorRecents.getValue();
    rapidjson::Document doc;
    doc.Parse(raw.toUtf8().constData());
    if (doc.HasParseError() || !doc.IsArray())
    {
        return {};
    }
    bool error = false;
    auto list = pajlada::Deserialize<QStringList>::get(doc, &error);
    if (error)
    {
        return {};
    }

    QStringList out;
    for (const auto &entry : list)
    {
        out = pushChatColorRecent(std::move(out), entry, kChatColorRecentsLimit);
    }
    return out;
}

void saveChatColorRecents(const QStringList &recents)
{
    // Re-normalize and cap on write.
    QStringList capped;
    for (const auto &entry : recents)
    {
        capped = pushChatColorRecent(std::move(capped), entry,
                                     kChatColorRecentsLimit);
    }

    rapidjson::Document doc;
    auto value =
        pajlada::Serialize<QStringList>::get(capped, doc.GetAllocator());
    rapidjson::StringBuffer buf;
    rapidjson::Writer<rapidjson::StringBuffer> writer(buf);
    value.Accept(writer);
    getSettings()->limerinoChatColorRecents.setValue(
        QString::fromUtf8(buf.GetString(),
                          static_cast<int>(buf.GetSize())));
}

QString loadLastChatColor()
{
    return normalizeChatColorValue(
        getSettings()->limerinoChatColorLast.getValue());
}

void saveLastChatColor(const QString &value)
{
    getSettings()->limerinoChatColorLast.setValue(
        normalizeChatColorValue(value));
}

}  // namespace chatterino::limerino
