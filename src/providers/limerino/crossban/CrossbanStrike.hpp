// SPDX-License-Identifier: MIT
// Parsed ChatModeratorStrikeStatus payload for crossban.

#pragma once

#include <QString>

class QJsonObject;

namespace chatterino::limerino {

enum class CrossbanStrikeKind {
    Clean,
    Banned,
    TimedOut,
    Warning,
};

struct CrossbanStrike {
    CrossbanStrikeKind kind = CrossbanStrikeKind::Clean;
    QString reason;
    QString actorLogin;       // bannedBy / timedOutBy login
    QString actorDisplayName;
    QString createdAt;        // ISO, may be empty
    QString expiresAt;        // ISO; timeouts only
    qint64 expiresInMs = -1;  // remaining; timeouts only, -1 if unknown

    QString statusLabel() const;
    QString detailLabel() const;
};

/// Parse `data` from ChatModeratorStrikeStatus (the object under
/// chatModeratorStrikeStatus, or the whole data wrapper). Returns Clean on
/// malformed input.
CrossbanStrike parseStrikeStatus(const QJsonObject &data);

}  // namespace chatterino::limerino
