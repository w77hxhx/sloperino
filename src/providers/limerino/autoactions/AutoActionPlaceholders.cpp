// SPDX-License-Identifier: MIT

#include "providers/limerino/autoactions/AutoActionPlaceholders.hpp"

#include "common/QLogging.hpp"

#include <QRegularExpression>
#include <QStringList>

namespace chatterino::limerino {

namespace {

/// Built-in placeholder table. Anything not listed here is *unknown* and
/// expands to empty (and logs). {msg.text} is intentionally absent in v1.
QString lookup(const QString &key, const AutoActionContext &ctx,
               bool *ok)
{
    *ok = true;
    if (key == QLatin1String("msg.id"))
    {
        if (ctx.msgId.isEmpty())
        {
            *ok = false;
            return {};
        }
        return ctx.msgId;
    }
    if (key == QLatin1String("sender.name"))
    {
        return ctx.senderLogin;
    }
    if (key == QLatin1String("sender.displayName"))
    {
        return ctx.senderDisplayName;
    }
    if (key == QLatin1String("sender.id"))
    {
        return ctx.senderId;
    }
    if (key == QLatin1String("channel.name"))
    {
        return ctx.channelName;
    }
    if (key == QLatin1String("channel.id"))
    {
        if (ctx.channelId.isEmpty())
        {
            *ok = false;
            return {};
        }
        return ctx.channelId;
    }
    if (key == QLatin1String("platform"))
    {
        return ctx.platform;
    }

    *ok = false;
    return {};
}

}  // namespace

std::optional<QString> expandAutoAction(const QString &templ,
                                        const AutoActionContext &ctx,
                                        bool logUnknowns)
{
    QString out;
    out.reserve(templ.size() * 2);

    // {{ -> literal {, }} -> literal }. All other braces delimit a key.
    const QRegularExpression re(QStringLiteral(
        R"(\{\{|\}\}|\{([^{}]+)\})"));
    auto it = re.globalMatch(templ);
    qsizetype last = 0;

    QStringList unknowns;

    while (it.hasNext())
    {
        const auto m = it.next();
        out += templ.mid(last, m.capturedStart() - last);
        const auto token = m.captured(0);
        if (token == QLatin1String("{{"))
        {
            out += QLatin1Char('{');
        }
        else if (token == QLatin1String("}}"))
        {
            out += QLatin1Char('}');
        }
        else
        {
            const auto key = m.captured(1);
            bool ok = false;
            const auto val = lookup(key, ctx, &ok);
            if (!ok)
            {
                // {msg.id} / {channel.id} may legitimately be unavailable on
                // some channels: treat as skip (not error).
                if (key != QLatin1String("msg.id") &&
                    key != QLatin1String("channel.id") && logUnknowns &&
                    !unknowns.contains(key))
                {
                    unknowns.append(key);
                }
            }
            else
            {
                out += val;
            }
        }
        last = m.capturedEnd();
    }
    out += templ.mid(last);

    if (logUnknowns && !unknowns.isEmpty())
    {
        qCWarning(chatterinoMessage)
            << "Auto-action template: unknown placeholder(s)"
            << unknowns.join(", ");
    }

    const auto trimmed = out.trimmed();
    if (trimmed.isEmpty())
    {
        return std::nullopt;
    }
    return trimmed;
}

std::optional<QString> validateAutoActionTemplate(const QString &templ)
{
    if (templ.trimmed().isEmpty())
    {
        return QStringLiteral("Action is empty.");
    }

    // Just check for unknown placeholder names.
    const QRegularExpression re(QStringLiteral(R"(\{([^{}]+)\})"));
    auto it = re.globalMatch(templ);
    QStringList unknown;
    while (it.hasNext())
    {
        const auto key = it.next().captured(1);
        if (key != QLatin1String("msg.id") &&
            key != QLatin1String("sender.name") &&
            key != QLatin1String("sender.displayName") &&
            key != QLatin1String("sender.id") &&
            key != QLatin1String("channel.name") &&
            key != QLatin1String("channel.id") &&
            key != QLatin1String("platform"))
        {
            unknown.append(key);
        }
    }
    if (!unknown.isEmpty())
    {
        return QStringLiteral(
                   "Unknown placeholder(s): %1 (they will expand to empty).")
            .arg(unknown.join(QStringLiteral(", ")));
    }
    return std::nullopt;
}

}  // namespace chatterino::limerino
