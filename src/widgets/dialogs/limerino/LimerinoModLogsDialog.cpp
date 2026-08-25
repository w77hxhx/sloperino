// SPDX-License-Identifier: MIT

#include "widgets/dialogs/limerino/LimerinoModLogsDialog.hpp"

#include "providers/limerino/gql/LimerinoGql.hpp"
#include "providers/limerino/gql/PersistedQueries.hpp"
#include "providers/limerino/LimerinoAuth.hpp"
#include "providers/limerino/LimerinoErrors.hpp"
#include "providers/twitch/TwitchChannel.hpp"
#include "widgets/dialogs/limerino/LimerinoResultList.hpp"

#include <QDateTime>
#include <QJsonArray>
#include <QJsonObject>
#include <QLabel>
#include <QPointer>
#include <QUrl>
#include <QVBoxLayout>

#include <algorithm>

namespace chatterino::limerino {

namespace gql = chatterino::LimerinoAuth::gql;

namespace {

// Hard cap: 100 pages of 100 edges = 10k actions.
constexpr int MAX_PAGES = 100;

// Same extraction rule as the plugin: first localized fragment with a
// displayName token is the moderator's name.
QString modNameFromNode(const QJsonObject &node)
{
    for (const QJsonValue &v :
         node[QStringLiteral("content")]
             .toObject()[QStringLiteral("localizedStringFragments")]
             .toArray())
    {
        const QString name = v.toObject()
                                 [QStringLiteral("token")]
                                 .toObject()[QStringLiteral("displayName")]
                                 .toString();
        if (!name.isEmpty())
        {
            return name;
        }
    }
    return {};
}

}  // namespace

LimerinoModLogsDialog::LimerinoModLogsDialog(
    const QString &channelLogin, const QString &channelId,
    const QString &userLabel, int days, QWidget *parent)
    : BasePopup({BaseWindow::Flags::Dialog}, parent)
    , channelLogin_(channelLogin)
    , channelId_(channelId)
    , userLabel_(userLabel)
    , days_(days)
{
    this->setAttribute(Qt::WA_DeleteOnClose);
    this->setWindowTitle(QStringLiteral("Mod logs - %1").arg(userLabel));
    this->resize(560, 640);

    auto *root = new QVBoxLayout(this);
    this->summaryLabel_ = new QLabel(this);
    this->summaryLabel_->setWordWrap(true);
    root->addWidget(this->summaryLabel_);

    this->modsTable_ = new LimerinoResultList(this);
    this->modsTable_->setTitleText(
        QStringLiteral("Moderators (click a name to open their channel)"));
    this->modsTable_->setColumns(
        {QStringLiteral("moderator"), QStringLiteral("total"),
         QStringLiteral("bans"), QStringLiteral("timeouts"),
         QStringLiteral("unbans"), QStringLiteral("untimeouts")});
    this->modsTable_->setNumericColumns({1, 2, 3, 4, 5});
    this->modsTable_->setRowOpenUrlProvider([](const QStringList &row) {
        const QString name = row.value(0);
        return name.isEmpty() ? QString()
                              : QStringLiteral("https://www.twitch.tv/%1")
                                    .arg(name);
    });
    root->addWidget(this->modsTable_, 1);

    this->actionsTable_ = new LimerinoResultList(this);
    this->actionsTable_->setTitleText(QStringLiteral("Actions"));
    this->actionsTable_->setColumns({QStringLiteral("time"),
                                     QStringLiteral("mod"),
                                     QStringLiteral("action"),
                                     QStringLiteral("type")});
    root->addWidget(this->actionsTable_, 1);

    this->summaryLabel_->setText(QStringLiteral("fetching..."));

    QString err;
    auto token = LimerinoAuth::resolveModerationToken(channelId, channelLogin,
                                                      &err);
    if (!token.hasToken())
    {
        this->summaryLabel_->setText(
            err.isEmpty()
                ? LimerinoAuth::errors::tokenRequiredMessage(
                      QStringLiteral("view moderator actions"))
                : err);
        return;
    }
    this->token_ = token.token;
    this->fetchPage(QString());
}

void LimerinoModLogsDialog::fetchPage(const QString &cursor)
{
    gql::executePersisted(
        gql::PQ_MOD_ACTIONS_LIST,
        QJsonObject{{QStringLiteral("channelID"), this->channelId_},
                    {QStringLiteral("after"), cursor}},
        this->token_,
        [g = QPointer<LimerinoModLogsDialog>(this), this](
            const QJsonObject &data) {
            if (!g)
            {
                return;
            }
            const QJsonObject logs =
                data[QStringLiteral("channel")].toObject()[
                    QStringLiteral("moderationActionLogs")].toObject();
            if (logs.isEmpty())
            {
                this->summaryLabel_->setText(
                    QStringLiteral("Failed to fetch mod actions."));
                return;
            }
            const QJsonArray edges = logs[QStringLiteral("edges")].toArray();
            bool keepGoing = false;
            QString nextCursor;

            const QDateTime firstTs = QDateTime::fromString(
                edges.isEmpty()
                    ? QString()
                    : edges.first()
                          .toObject()[QStringLiteral("node")]
                          .toObject()[QStringLiteral("createdAt")]
                          .toString(),
                Qt::ISODate);
            if (!this->cutoff_.isValid() && firstTs.isValid())
            {
                this->cutoff_ = firstTs.addDays(-this->days_);
            }

            for (const QJsonValue &v : edges)
            {
                const QJsonObject node = v.toObject()[QStringLiteral("node")]
                                             .toObject();
                const QString createdAt =
                    node[QStringLiteral("createdAt")].toString();
                const QDateTime ts = QDateTime::fromString(createdAt,
                                                           Qt::ISODate);
                if (this->cutoff_.isValid() && ts < this->cutoff_)
                {
                    continue;  // beyond the day window
                }
                const QString category =
                    node[QStringLiteral("filterCategoryID")].toString();
                if (category != QStringLiteral("BANS_AND_UNBANS") &&
                    category != QStringLiteral("TIMEOUTS_AND_UNTIMEOUTS"))
                {
                    continue;  // plugin counts only those two categories
                }
                const QString icon = node[QStringLiteral("icon")].toString();
                const QString modName = modNameFromNode(node);

                this->actionsRow_.append(
                    {ts.toLocalTime().toString(QStringLiteral("yyyy-MM-dd hh:mm:ss")),
                     modName, icon, category});

                if (modName.isEmpty())
                {
                    continue;
                }
                auto &st = this->modStats_[modName];
                st.name = modName;
                if (category == QStringLiteral("BANS_AND_UNBANS"))
                {
                    if (icon == QStringLiteral("BAN"))
                    {
                        st.bans++;
                        this->totalBans_++;
                    }
                    else
                    {
                        st.unbans++;
                        this->totalUnbans_++;
                    }
                }
                else
                {
                    if (icon == QStringLiteral("TIMEOUT"))
                    {
                        st.timeouts++;
                        this->totalTimeouts_++;
                    }
                    else
                    {
                        st.untimeouts++;
                        this->totalUntimeouts_++;
                    }
                }
                st.total++;
                ++this->totalActions_;
            }

            if (!edges.isEmpty())
            {
                nextCursor = edges.last()
                                 .toObject()[QStringLiteral("cursor")]
                                 .toString();
                keepGoing = !nextCursor.isEmpty() &&
                            ++this->pages_ < MAX_PAGES &&
                            (!this->cutoff_.isValid() ||
                             QDateTime::fromString(
                                 edges.last()
                                     .toObject()[QStringLiteral("node")]
                                     .toObject()[QStringLiteral("createdAt")]
                                     .toString(),
                                 Qt::ISODate) >= this->cutoff_);
            }

            this->summaryLabel_->setText(
                QStringLiteral("fetching... %1 actions (%2 pages)")
                    .arg(this->totalActions_)
                    .arg(this->pages_));

            if (keepGoing)
            {
                this->fetchPage(nextCursor);
            }
            else
            {
                this->finish();
            }
        },
        [g = QPointer<LimerinoModLogsDialog>(this)](const gql::GqlError &e) {
            if (g)
            {
                g->summaryLabel_->setText(
                    QStringLiteral("Failed to fetch mod actions! %1")
                        .arg(e.message));
            }
        },
        10000);
}

void LimerinoModLogsDialog::finish()
{
    QVector<ModStat> stats = this->modStats_.values().toVector();
    std::sort(stats.begin(), stats.end(),
              [](const ModStat &a, const ModStat &b) {
                  return a.total > b.total;
              });

    QVector<QStringList> rows;
    for (const ModStat &s : stats)
    {
        rows.append({s.name, QString::number(s.total),
                     QString::number(s.bans), QString::number(s.timeouts),
                     QString::number(s.unbans), QString::number(s.untimeouts)});
    }
    this->modsTable_->setRows(rows);
    this->modsTable_->setStatusText(
        QStringLiteral("Total Actions: %1 (Bans: %2 | Timeouts: %3) - Active "
                       "Moderators: %4 - Last %5 days")
            .arg(this->totalActions_)
            .arg(this->totalBans_)
            .arg(this->totalTimeouts_)
            .arg(stats.size())
            .arg(this->days_));

    QVector<QStringList> actionRows(this->actionsRow_);
    std::reverse(actionRows.begin(), actionRows.end());  // newest first
    this->actionsTable_->setRows(actionRows);
    this->summaryLabel_->setText(
        QStringLiteral("Mod Actions Summary for %1 (Last %2 Days)")
            .arg(this->userLabel_)
            .arg(this->days_));
}

}  // namespace chatterino::limerino
