// SPDX-FileCopyrightText: 2026 Contributors to Chatterino <https://chatterino.com>
//
// SPDX-License-Identifier: MIT

#include "widgets/settingspages/SloperinoPage.hpp"

#include "singletons/Settings.hpp"
#include "widgets/BaseWidget.hpp"
#include "widgets/settingspages/GeneralPageView.hpp"
#include "widgets/settingspages/SettingWidget.hpp"

#include <QFrame>
#include <QHBoxLayout>
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

    // 1. Badges Category
    layout.addTitle("Badges");
    layout.addDescription("Toggle Sloperino badges.");

    SettingWidget::checkbox("Show Sloperino badges", s.showSloperinoBadges)
        ->addKeywords({"badges", "sloperino"})
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

    layout.addStretch();

    // Invisible element for width
    auto *inv = new BaseWidget(this);
    layout.addWidget(inv);
}

}  // namespace chatterino
