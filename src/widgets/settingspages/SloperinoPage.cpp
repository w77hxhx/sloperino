// SPDX-FileCopyrightText: 2026 Contributors to Chatterino <https://chatterino.com>
//
// SPDX-License-Identifier: MIT

#include "widgets/settingspages/SloperinoPage.hpp"

#include "controllers/aliases/EmoteAlias.hpp"
#include "controllers/aliases/EmoteAliasesModel.hpp"
#include "singletons/Settings.hpp"
#include "widgets/BaseWidget.hpp"
#include "widgets/helper/EditableModelView.hpp"
#include "widgets/settingspages/GeneralPageView.hpp"
#include "widgets/settingspages/SettingWidget.hpp"

#include <QFrame>
#include <QHBoxLayout>
#include <QHeaderView>
#include <QTableView>
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

    SettingWidget::checkbox("wss://logs.spanix.team/firehose",
                            s.firehoseEnableSpanix)
        ->addKeywords({"spanix", "firehose", "logs"})
        ->setTooltip("Enable or disable the Spanix firehose WebSocket feed.")
        ->addTo(layout);

    SettingWidget::checkbox("wss://logs.supa.codes/firehose",
                            s.firehoseEnableSupa)
        ->addKeywords({"supa", "firehose", "logs"})
        ->setTooltip("Enable or disable the Supa firehose WebSocket feed.")
        ->addTo(layout);

    SettingWidget::checkbox("wss://logs.susgee.dev/firehose",
                            s.firehoseEnableSusgee)
        ->addKeywords({"susgee", "firehose", "logs"})
        ->setTooltip("Enable or disable the Susgee firehose WebSocket feed.")
        ->addTo(layout);

    SettingWidget::checkbox("wss://logs.nadeko.net/firehose",
                            s.firehoseEnableNadeko)
        ->addKeywords({"nadeko", "firehose", "logs"})
        ->setTooltip("Enable or disable the Nadeko firehose WebSocket feed.")
        ->addTo(layout);

    SettingWidget::checkbox("wss://logxx.dev/firehose", s.firehoseEnableLogxx)
        ->addKeywords({"logxx", "firehose", "logs"})
        ->setTooltip("Enable or disable the Logxx firehose WebSocket feed.")
        ->addTo(layout);

    SettingWidget::checkbox("wss://firehose.catquery.com",
                            s.firehoseEnableCatquery)
        ->addKeywords({"catquery", "firehose", "logs"})
        ->setTooltip("Enable or disable the Catquery firehose WebSocket feed.")
        ->addTo(layout);

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

}  // namespace chatterino
