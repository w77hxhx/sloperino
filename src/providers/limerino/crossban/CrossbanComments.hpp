// SPDX-License-Identifier: MIT
// ViewerCardModLogsComments parse + createModComment helper.

#pragma once

#include <QString>
#include <QVector>

#include <functional>

class QJsonObject;

namespace chatterino::limerino {

struct CrossbanComment {
    QString id;
    QString text;
    QString timestamp;
    QString authorLogin;
    QString authorDisplayName;
    bool isShareable = true;
};

QVector<CrossbanComment> parseModComments(const QJsonObject &data);

using CrossbanCommentsSuccess =
    std::function<void(const QVector<CrossbanComment> &)>;
using CrossbanCommentCreated = std::function<void(const CrossbanComment &)>;
using CrossbanCommentsError = std::function<void(const QString &)>;

void fetchModComments(const QString &channelId, const QString &targetId,
                      const QString &gqlToken, CrossbanCommentsSuccess onOk,
                      CrossbanCommentsError onErr);

void createModComment(const QString &channelId, const QString &targetId,
                      const QString &text, const QString &gqlToken,
                      CrossbanCommentCreated onOk, CrossbanCommentsError onErr);

}  // namespace chatterino::limerino
