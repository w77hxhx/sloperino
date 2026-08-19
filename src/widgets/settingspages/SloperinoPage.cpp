// SPDX-FileCopyrightText: 2026 Contributors to Chatterino <https://chatterino.com>
//
// SPDX-License-Identifier: MIT

#include "widgets/settingspages/SloperinoPage.hpp"

#include "Application.hpp"
#include "controllers/aliases/EmoteAlias.hpp"
#include "controllers/aliases/EmoteAliasesModel.hpp"
#include "providers/firehose/FirehoseManager.hpp"
#include "singletons/Settings.hpp"
#include "widgets/BaseWidget.hpp"
#include "widgets/helper/EditableModelView.hpp"
#include "widgets/settingspages/GeneralPageView.hpp"
#include "widgets/settingspages/SettingWidget.hpp"

#include <QCheckBox>
#include <QFrame>
#include <QHBoxLayout>
#include <QHeaderView>
#include <QLabel>
#include <QTableView>
#include <QTimer>
#include <QVBoxLayout>

namespace chatterino {

SloperinoPage::SloperinoPage()
{
    auto *outer = new QVBoxLayout;
    auto *inner = new QHBoxLayout;
    auto *view = GeneralPageView::withNavigation(this);
    this->view_ = view;

    inner->addWidget(view);
    auto *frame = new QFrame;
    frame->setLayout(inner);
    outer->addWidget(frame);
    this->setLayout(outer);

    this->initLayout(*view);
}

bool SloperinoPage::filterElements(const QString &query)
{
    if (this->view_)
    {
        return this->view_->filterElements(query) || query.isEmpty();
    }

    return false;
}

void SloperinoPage::initLayout(GeneralPageView &layout)
{
    auto &s = *getSettings();

    // 1. Usercard Category
    layout.addTitle("Usercard");
    layout.addDescription("Customize usercard buttons, details, and widgets.");

    SettingWidget::checkbox("Show clips button", s.showUsercardClipsButton)
        ->setTooltip("Show a button on usercards to view and search the user's "
                     "Twitch clips.")
        ->addKeywords({"usercard", "clips", "twitch", "button", "video"})
        ->addTo(layout);

    SettingWidget::checkbox("Show roles button", s.showUsercardRolesButton)
        ->setTooltip("Show a button on usercards to look up Twitch roles via "
                     "roles.tv.")
        ->addKeywords({"usercard", "roles", "roles.tv", "button", "moderator",
                       "vip", "artist"})
        ->addTo(layout);

    // 2. Firehose Category
    layout.addTitle("Firehose");
    layout.addDescription(
        "Real-time Twitch chat firehose WebSocket streaming options.");

    SettingWidget::checkbox("Auto-reconnect on disconnect",
                            s.firehoseAutoReconnect)
        ->addKeywords({"firehose", "reconnect", "websocket"})
        ->setTooltip("Automatically reconnect to firehose WebSocket servers "
                     "if the connection is dropped.")
        ->addTo(layout);

    SettingWidget::checkbox("Show message rate in header",
                            s.firehoseShowRateInTitle)
        ->addKeywords({"firehose", "rate", "speed", "mps", "msg/s"})
        ->setTooltip(
            "Show current messages-per-second and active socket count in "
            "the Firehose tab title.")
        ->addTo(layout);

    SettingWidget::intInput("Max stored messages", s.firehoseMaxMessages,
                            {.min = 100, .max = 50000, .singleStep = 500})
        ->addKeywords({"firehose", "buffer", "limit", "messages"})
        ->setTooltip("Maximum number of messages to retain in the "
                     "Firehose channel.")
        ->addTo(layout);

    SettingWidget::intInput("Batch update interval", s.firehoseBatchIntervalMs,
                            {.min = 50, .max = 2000, .singleStep = 50})
        ->addKeywords({"firehose", "batch", "interval", "render"})
        ->setTooltip("Interval (in ms) to batch and render incoming firehose "
                     "messages. Higher values reduce CPU usage.")
        ->addTo(layout);

    layout.addSubtitle("Firehose Endpoints");
    layout.addDescription(
        "Select active public Twitch chat firehose data sources:");

    // Build a per-endpoint row: [checkbox][url label][status badge]
    struct EndpointRow {
        BoolSetting *setting;
        QString label;
    };
    auto &s2 = s;
    const std::vector<EndpointRow> rows = {
        {&s2.firehoseEnableSpanix, "wss://logs.spanix.team/firehose"},
        {&s2.firehoseEnableSupa, "wss://logs.supa.codes/firehose"},
        {&s2.firehoseEnableSusgee, "wss://logs.susgee.dev/firehose"},
        {&s2.firehoseEnableNadeko, "wss://logs.nadeko.net/firehose"},
        {&s2.firehoseEnableLogxx, "wss://logxx.dev/firehose"},
        {&s2.firehoseEnableCatquery, "wss://firehose.catquery.com"},
    };

    this->endpointStatusLabels_.clear();
    for (const auto &row : rows)
    {
        // Container widget for the row
        auto *rowWidget = new QWidget;
        auto *rowLayout = new QHBoxLayout(rowWidget);
        rowLayout->setContentsMargins(0, 0, 0, 0);
        rowLayout->setSpacing(6);

        // Status badge label (updated by timer)
        auto *badge = new QLabel("●");
        badge->setFixedWidth(14);
        badge->setStyleSheet("color: #666; font-size: 10px;");
        rowLayout->addWidget(badge);
        this->endpointStatusLabels_.push_back(badge);

        // URL label
        auto *urlLabel = new QLabel(row.label);
        urlLabel->setStyleSheet("font-family: monospace; font-size: 12px;");
        rowLayout->addWidget(urlLabel, 1);

        // Enabled checkbox
        auto *cb = new QCheckBox;
        cb->setChecked(row.setting->getValue());
        QObject::connect(cb, &QCheckBox::toggled,
                         [setting = row.setting](bool v) {
                             setting->setValue(v);
                         });
        // React to external setting changes
        QObject::connect(cb, &QCheckBox::destroyed, [] {});
        rowLayout->addWidget(cb);

        layout.addWidget(rowWidget);
    }

    // Refresh status every 1 second
    this->statusRefreshTimer_ = new QTimer(this);
    this->statusRefreshTimer_->setInterval(1000);
    QObject::connect(this->statusRefreshTimer_, &QTimer::timeout, this,
                     &SloperinoPage::refreshEndpointStatuses);
    this->statusRefreshTimer_->start();
    // Initial update
    this->refreshEndpointStatuses();

    // 3. Fun Category
    layout.addTitle("Fun");
    layout.addDescription("Fun and experimental chat options.");

    SettingWidget::checkbox("Random client mode (client-nonce)",
                            s.randomClientNonce)
        ->addKeywords(
            {"fun", "client", "random", "nonce", "ios", "android", "web"})
        ->setTooltip(
            "Send chat messages with a randomized client-nonce (simulating "
            "Web, iOS, and Android clients on Twitch).")
        ->addTo(layout);

    // 4. Aliases Category
    layout.addTitle("Aliases");
    layout.addDescription("Replace specific words in chat with custom 7TV, "
                          "BTTV, FFZ, or direct CDN emote links.");

    auto *aliasesModel =
        (new EmoteAliasesModel(nullptr))->initialized(&s.customEmoteAliases);
    auto *aliasesView = new EditableModelView(aliasesModel, true);
    aliasesView->setTitles(
        {"Word", "Link (7TV / BTTV / FFZ / CDN)", "Case-sensitive"});
    aliasesView->getTableView()->horizontalHeader()->setSectionResizeMode(
        QHeaderView::Fixed);
    aliasesView->getTableView()->horizontalHeader()->setSectionResizeMode(
        0, QHeaderView::Interactive);
    aliasesView->getTableView()->horizontalHeader()->setSectionResizeMode(
        1, QHeaderView::Stretch);
    aliasesView->getTableView()->setColumnWidth(0, 160);
    aliasesView->getTableView()->setMinimumHeight(180);

    std::ignore = aliasesView->addButtonPressed.connect([] {
        getSettings()->customEmoteAliases.append(EmoteAlias{
            "Привет", "https://7tv.app/emotes/01H3YN7XBG000BH97SCKY1D88B",
            false});
    });

    layout.addWidget(aliasesView,
                     {"aliases", "emote", "replace", "7tv", "bttv", "ffz"});

    layout.addStretch();

    // Invisible element for width
    auto *inv = new BaseWidget(this);
    layout.addWidget(inv);
}

void SloperinoPage::refreshEndpointStatuses()
{
    using Status = FirehoseManager::EndpointStatus;

    const auto *fh = getApp()->getFirehose();
    if (!fh)
    {
        return;
    }
    const auto statuses = fh->getEndpointStatuses();
    for (int i = 0;
         i < statuses.size() && i < this->endpointStatusLabels_.size(); ++i)
    {
        auto *badge = this->endpointStatusLabels_[i];
        const auto &info = statuses[i];

        switch (info.status)
        {
            case Status::Connected:
                badge->setStyleSheet(
                    "color: #2ecc71; font-size: 10px;");  // green
                badge->setToolTip("Connected");
                break;
            case Status::Connecting:
                badge->setStyleSheet(
                    "color: #f39c12; font-size: 10px;");  // orange
                badge->setToolTip("Connecting...");
                break;
            case Status::Reconnecting:
                badge->setStyleSheet(
                    "color: #e67e22; font-size: 10px;");  // amber
                badge->setToolTip(QStringLiteral("Reconnecting... (backoff)"));
                break;
            case Status::Disabled:
            default:
                badge->setStyleSheet("color: #555; font-size: 10px;");  // grey
                badge->setToolTip(info.enabled ? "Disconnected" : "Disabled");
                break;
        }
    }
}

}  // namespace chatterino
