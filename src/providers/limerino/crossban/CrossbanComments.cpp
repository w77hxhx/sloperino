// SPDX-License-Identifier: MIT

#include "providers/limerino/crossban/CrossbanComments.hpp"

#include "providers/limerino/gql/LimerinoGql.hpp"
#include "providers/limerino/gql/PersistedQueries.hpp"

#include <QJsonArray>
#include <QJsonObject>

namespace chatterino::limerino {

namespace gql = chatterino::LimerinoAuth::gql;

namespace {

CrossbanComment commentFromNode(const QJsonObject &node)
{
    CrossbanComment c;
    c.id = node[QStringLiteral("id")].toString();
    c.text = node[QStringLiteral("text")].toString();
    c.timestamp = node[QStringLiteral("timestamp")].toString();
    c.isShareable = node[QStringLiteral("isShareable")].toBool(true);
    const auto author = node[QStringLiteral("author")].toObject();
    c.authorLogin = author[QStringLiteral("login")].toString();
    c.authorDisplayName = author[QStringLiteral("displayName")].toString();
    return c;
}

}  // namespace

QVector<CrossbanComment> parseModComments(const QJsonObject &data)
{
    QJsonObject logs = data;
    if (data.contains(QStringLiteral("viewerCardModLogs")))
    {
        logs = data[QStringLiteral("viewerCardModLogs")].toObject();
    }
    const auto comments = logs[QStringLiteral("comments")].toObject();
    const auto edges = comments[QStringLiteral("edges")].toArray();

    QVector<CrossbanComment> out;
    out.reserve(edges.size());
    for (const QJsonValue edgeVal : edges)
    {
        const auto node = edgeVal.toObject()[QStringLiteral("node")].toObject();
        if (node.isEmpty())
        {
            continue;
        }
        out.append(commentFromNode(node));
    }
    return out;
}

void fetchModComments(const QString &channelId, const QString &targetId,
                      const QString &gqlToken, CrossbanCommentsSuccess onOk,
                      CrossbanCommentsError onErr)
{
    gql::executePersisted(
        gql::PQ_VIEWER_CARD_MOD_LOGS_COMMENTS,
        QJsonObject{{QStringLiteral("channelID"), channelId},
                    {QStringLiteral("targetID"), targetId}},
        gqlToken,
        [onOk](const QJsonObject &data) {
            if (onOk)
            {
                onOk(parseModComments(data));
            }
        },
        [onErr](const gql::GqlError &err) {
            if (onErr)
            {
                onErr(err.message);
            }
        });
}

void createModComment(const QString &channelId, const QString &targetId,
                      const QString &text, const QString &gqlToken,
                      CrossbanCommentCreated onOk, CrossbanCommentsError onErr)
{
    gql::executePersisted(
        gql::PQ_CREATE_MOD_COMMENT,
        QJsonObject{
            {QStringLiteral("input"),
             QJsonObject{{QStringLiteral("channelID"), channelId},
                         {QStringLiteral("targetID"), targetId},
                         {QStringLiteral("text"), text},
                         {QStringLiteral("isShareable"), true}}},
        },
        gqlToken,
        [onOk, onErr](const QJsonObject &data) {
            const auto payload =
                data[QStringLiteral("createModeratorComment")].toObject();
            const auto comment =
                payload[QStringLiteral("comment")].toObject();
            if (comment.isEmpty())
            {
                if (onErr)
                {
                    onErr(QStringLiteral("createModComment returned no comment"));
                }
                return;
            }
            if (onOk)
            {
                onOk(commentFromNode(comment));
            }
        },
        [onErr](const gql::GqlError &err) {
            if (onErr)
            {
                onErr(err.message);
            }
        });
}

}  // namespace chatterino::limerino
