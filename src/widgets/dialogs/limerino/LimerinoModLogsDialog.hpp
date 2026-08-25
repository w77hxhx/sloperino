// SPDX-License-Identifier: MIT
// Mod action log window for /modlogs (batch 10). Fetches persisted
// ModActionsList pages day-window-limited, shows a per-moderator breakdown
// (sortable numerically, clickable moderators) and the full action list.

#pragma once

#include "widgets/BasePopup.hpp"

#include <QDateTime>
#include <QMap>
#include <QString>
#include <QVector>

class QJsonObject;
class QLabel;

namespace chatterino {

class Split;
class TwitchChannel;

namespace limerino {

class LimerinoResultList;

class LimerinoModLogsDialog final : public BasePopup
{
    Q_OBJECT

public:
    // channelID/userlabel used to title the window; days limits the window.
    LimerinoModLogsDialog(const QString &channelLogin, const QString &channelId,
                          const QString &userLabel, int days,
                          QWidget *parent = nullptr);

private:
    void fetchPage(const QString &cursor);
    void finish();

    QString channelLogin_;
    QString channelId_;
    QString userLabel_;
    int days_ = 30;

    QString token_;
    int pages_ = 0;
    QDateTime cutoff_;

    struct ModStat {
        QString name;
        int total = 0;
        int bans = 0;
        int timeouts = 0;
        int unbans = 0;
        int untimeouts = 0;
    };
    QMap<QString, ModStat> modStats_;  // keyed by display name
    int totalActions_ = 0;
    int totalBans_ = 0;
    int totalTimeouts_ = 0;
    int totalUnbans_ = 0;
    int totalUntimeouts_ = 0;

    LimerinoResultList *modsTable_ = nullptr;
    LimerinoResultList *actionsTable_ = nullptr;
    QLabel *summaryLabel_ = nullptr;
    QVector<QStringList> actionsRow_;
};

}  // namespace limerino
}  // namespace chatterino
