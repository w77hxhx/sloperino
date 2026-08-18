#include "providers/roles/RolesApi.hpp"

#include "common/network/NetworkRequest.hpp"
#include "common/network/NetworkResult.hpp"

#include <QJsonArray>
#include <QJsonDocument>
#include <QJsonObject>
#include <QUrl>
#include <QUrlQuery>

namespace chatterino {

namespace {

RoleSummary parseSummary(const QJsonObject &data)
{
    RoleSummary summary;
    summary.id = data["id"].toString();
    summary.login = data["login"].toString();
    summary.displayName = data["displayName"].toString();
    summary.chatColor = data["chatColor"].toString();
    summary.description = data["description"].toString();
    summary.avatar = data["avatar"].toString();
    summary.followers = data["followers"].toInt();
    summary.isPartner = data["isPartner"].toBool();
    summary.isAffiliate = data["isAffiliate"].toBool();
    summary.isBanned = data["isBanned"].toBool();

    if (data.contains("roles") && data["roles"].isObject())
    {
        auto roles = data["roles"].toObject();
        summary.moderators = roles["moderators"].toInt();
        summary.vips = roles["vips"].toInt();
        summary.artists = roles["artists"].toInt();
        summary.founders = roles["founders"].toInt();
        summary.subscribers = roles["subscribers"].toInt();
    }

    return summary;
}

RoleItem parseRoleItem(const QJsonObject &obj)
{
    RoleItem item;
    item.id = obj["id"].toString();
    item.login = obj["login"].toString();
    item.displayName = obj["displayName"].toString();
    item.chatColor = obj["chatColor"].toString();
    item.description = obj["description"].toString();
    item.avatar = obj["avatar"].toString();
    item.followers = obj["followers"].toInt();
    item.isPartner = obj["isPartner"].toBool();
    item.isAffiliate = obj["isAffiliate"].toBool();
    item.isBanned = obj["isBanned"].toBool();
    item.active = obj["active"].toBool(true);

    if (obj.contains("grantedAt") && !obj["grantedAt"].isNull())
    {
        item.grantedAt =
            QDateTime::fromString(obj["grantedAt"].toString(), Qt::ISODate);
        if (!item.grantedAt.isValid())
        {
            item.grantedAt = QDateTime::fromString(
                obj["grantedAt"].toString(), "yyyy-MM-dd HH:mm:ss.zzzzzz");
        }
    }

    return item;
}

}  // namespace

void RolesApi::getChannelSummary(const QString &channelLogin,
                                 SummaryCallback success, ErrorCallback failure)
{
    const auto cleaned = channelLogin.trimmed().toLower();
    if (cleaned.isEmpty())
    {
        failure("Channel name cannot be empty");
        return;
    }

    const auto url =
        QUrl(QStringLiteral(
                 "https://roles.tv/api/channel/login/%1?sort=display_name")
                 .arg(QString::fromUtf8(QUrl::toPercentEncoding(cleaned))));

    NetworkRequest(url)
        .timeout(10000)
        .header("Accept", "application/json")
        .header("User-Agent", "Sloperino")
        .onSuccess([success = std::move(success),
                    failure](const NetworkResult &result) {
            const auto root = result.parseJson();
            if (root.isEmpty())
            {
                failure("Invalid JSON response from roles.tv");
                return;
            }

            if (!root.contains("data") || !root["data"].isObject())
            {
                failure("Data object missing in response");
                return;
            }

            success(parseSummary(root["data"].toObject()));
        })
        .onError([failure = std::move(failure)](const NetworkResult &result) {
            failure(result.formatError());
        })
        .execute();
}

void RolesApi::getUserSummary(const QString &userLogin, SummaryCallback success,
                              ErrorCallback failure)
{
    const auto cleaned = userLogin.trimmed().toLower();
    if (cleaned.isEmpty())
    {
        failure("User name cannot be empty");
        return;
    }

    const auto url =
        QUrl(QStringLiteral("https://roles.tv/api/user/login/%1")
                 .arg(QString::fromUtf8(QUrl::toPercentEncoding(cleaned))));

    NetworkRequest(url)
        .timeout(10000)
        .header("Accept", "application/json")
        .header("User-Agent", "Sloperino")
        .onSuccess([success = std::move(success),
                    failure](const NetworkResult &result) {
            const auto root = result.parseJson();
            if (root.isEmpty())
            {
                failure("Invalid JSON response from roles.tv");
                return;
            }

            if (!root.contains("data") || !root["data"].isObject())
            {
                failure("Data object missing in response");
                return;
            }

            success(parseSummary(root["data"].toObject()));
        })
        .onError([failure = std::move(failure)](const NetworkResult &result) {
            failure(result.formatError());
        })
        .execute();
}

void RolesApi::getRoleList(const QString &mode, const QString &role,
                           const QString &login, const QString &cursor,
                           ListCallback success, ErrorCallback failure)
{
    const auto cleanedMode = mode.trimmed().toLower();
    const auto cleanedRole = role.trimmed().toLower();
    const auto cleanedLogin = login.trimmed().toLower();

    if (cleanedLogin.isEmpty())
    {
        failure("Target login cannot be empty");
        return;
    }

    QUrl url(
        QStringLiteral("https://roles.tv/api/stats/%1/%2/login/%3")
            .arg(cleanedMode, cleanedRole,
                 QString::fromUtf8(QUrl::toPercentEncoding(cleanedLogin))));

    QUrlQuery query;
    query.addQueryItem("per_page", "100");
    if (!cursor.isEmpty())
    {
        query.addQueryItem("after", cursor);
    }
    url.setQuery(query);

    NetworkRequest(url)
        .timeout(12000)
        .header("Accept", "application/json")
        .header("User-Agent", "Sloperino")
        .onSuccess([cleanedMode, cleanedRole, success = std::move(success),
                    failure](const NetworkResult &result) {
            const auto root = result.parseJson();
            if (root.isEmpty())
            {
                failure("Invalid JSON response from roles.tv");
                return;
            }

            RoleListResult res;
            res.mode = cleanedMode;
            res.role = cleanedRole;
            res.total = root["total"].toInt();
            res.page = root["page"].toInt(1);
            res.pages = root["pages"].toInt(1);
            res.perPage = root["perPage"].toInt(100);

            if (root.contains("cursor") && !root["cursor"].isNull())
            {
                res.cursor = root["cursor"].toString();
            }

            if (root.contains("data") && root["data"].isArray())
            {
                const auto arr = root["data"].toArray();
                res.items.reserve(arr.size());
                for (const auto &val : arr)
                {
                    if (val.isObject())
                    {
                        res.items.push_back(parseRoleItem(val.toObject()));
                    }
                }
            }

            success(res);
        })
        .onError([failure = std::move(failure)](const NetworkResult &result) {
            failure(result.formatError());
        })
        .execute();
}

}  // namespace chatterino
