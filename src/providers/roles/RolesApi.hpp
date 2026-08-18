#pragma once

#include <QDateTime>
#include <QString>

#include <functional>
#include <vector>

namespace chatterino {

struct RoleSummary {
    QString id;
    QString login;
    QString displayName;
    QString chatColor;
    QString description;
    QString avatar;
    int followers = 0;
    bool isPartner = false;
    bool isAffiliate = false;
    bool isBanned = false;

    // Role counts
    int moderators = 0;
    int vips = 0;
    int artists = 0;
    int founders = 0;
    int subscribers = 0;

    int totalRoles() const
    {
        return this->moderators + this->vips + this->artists + this->founders +
               this->subscribers;
    }
};

struct RoleItem {
    QString id;
    QString login;
    QString displayName;
    QString chatColor;
    QString description;
    QString avatar;
    int followers = 0;
    bool isPartner = false;
    bool isAffiliate = false;
    bool isBanned = false;
    bool active = true;
    QDateTime grantedAt;
};

struct RoleListResult {
    QString mode;  // "channel" or "user"
    QString role;  // "moderators", "vips", "artists", "founders", "subscribers"
    int total = 0;
    int page = 1;
    int pages = 1;
    int perPage = 100;
    QString cursor;
    std::vector<RoleItem> items;
};

class RolesApi
{
public:
    using SummaryCallback = std::function<void(const RoleSummary &)>;
    using ListCallback = std::function<void(const RoleListResult &)>;
    using ErrorCallback = std::function<void(const QString &)>;

    static void getChannelSummary(const QString &channelLogin,
                                  SummaryCallback success,
                                  ErrorCallback failure);

    static void getUserSummary(const QString &userLogin,
                               SummaryCallback success, ErrorCallback failure);

    static void getRoleList(const QString &mode, const QString &role,
                            const QString &login, const QString &cursor,
                            ListCallback success, ErrorCallback failure);
};

}  // namespace chatterino
