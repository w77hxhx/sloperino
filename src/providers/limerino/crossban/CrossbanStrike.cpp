// SPDX-License-Identifier: MIT

#include "providers/limerino/crossban/CrossbanStrike.hpp"

#include <QJsonObject>

namespace chatterino::limerino {

namespace {

QJsonObject strikeRoot(const QJsonObject &data)
{
    if (data.contains(QStringLiteral("chatModeratorStrikeStatus")))
    {
        return data[QStringLiteral("chatModeratorStrikeStatus")].toObject();
    }
    return data;
}

}  // namespace

QString CrossbanStrike::statusLabel() const
{
    switch (this->kind)
    {
        case CrossbanStrikeKind::Clean:
            return QStringLiteral("Clean");
        case CrossbanStrikeKind::Banned:
            return QStringLiteral("Banned");
        case CrossbanStrikeKind::TimedOut:
            return QStringLiteral("Timed out");
        case CrossbanStrikeKind::Warning:
            return QStringLiteral("Warning");
    }
    return QStringLiteral("Clean");
}

QString CrossbanStrike::detailLabel() const
{
    QStringList parts;
    if (!this->reason.isEmpty())
    {
        parts.append(this->reason);
    }
    const QString by =
        this->actorDisplayName.isEmpty() ? this->actorLogin : this->actorDisplayName;
    if (!by.isEmpty())
    {
        parts.append(QStringLiteral("by %1").arg(by));
    }
    if (this->kind == CrossbanStrikeKind::TimedOut && !this->expiresAt.isEmpty())
    {
        parts.append(QStringLiteral("until %1").arg(this->expiresAt));
    }
    return parts.join(QStringLiteral(" · "));
}

CrossbanStrike parseStrikeStatus(const QJsonObject &data)
{
    CrossbanStrike out;
    const QJsonObject root = strikeRoot(data);
    if (root.isEmpty())
    {
        return out;
    }

    const QJsonObject ban = root[QStringLiteral("banDetails")].toObject();
    if (!ban.isEmpty())
    {
        out.kind = CrossbanStrikeKind::Banned;
        out.reason = ban[QStringLiteral("reason")].toString();
        out.createdAt = ban[QStringLiteral("createdAt")].toString();
        const auto by = ban[QStringLiteral("bannedBy")].toObject();
        out.actorLogin = by[QStringLiteral("login")].toString();
        out.actorDisplayName = by[QStringLiteral("displayName")].toString();
        return out;
    }

    const QJsonObject timeout = root[QStringLiteral("timeoutDetails")].toObject();
    if (!timeout.isEmpty())
    {
        out.kind = CrossbanStrikeKind::TimedOut;
        out.reason = timeout[QStringLiteral("reason")].toString();
        out.createdAt = timeout[QStringLiteral("createdAt")].toString();
        out.expiresAt = timeout[QStringLiteral("expiresAt")].toString();
        if (timeout.contains(QStringLiteral("expiresInMs")))
        {
            out.expiresInMs =
                static_cast<qint64>(timeout[QStringLiteral("expiresInMs")].toDouble());
        }
        const auto by = timeout[QStringLiteral("timedOutBy")].toObject();
        out.actorLogin = by[QStringLiteral("login")].toString();
        out.actorDisplayName = by[QStringLiteral("displayName")].toString();
        return out;
    }

    const QJsonObject warning = root[QStringLiteral("warningDetails")].toObject();
    if (!warning.isEmpty())
    {
        out.kind = CrossbanStrikeKind::Warning;
        // Shape uncaptured; mark present only.
        return out;
    }

    return out;
}

}  // namespace chatterino::limerino
