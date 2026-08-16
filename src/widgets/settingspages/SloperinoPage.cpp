// SPDX-FileCopyrightText: 2026 Contributors to Chatterino <https://chatterino.com>
//
// SPDX-License-Identifier: MIT

#include "widgets/settingspages/SloperinoPage.hpp"

#include "singletons/Settings.hpp"
#include "widgets/BaseWindow.hpp"
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
            "Display current messages per second (msg/s) in the split header.")
        ->addTo(layout);

    SettingWidget::intInput("Max stored messages", s.firehoseMaxMessages,
                            {.min = 1000,
                             .max = 50000,
                             .singleStep = 1000,
                             .suffix = QStringLiteral(" messages")})
        ->addKeywords({"firehose", "buffer", "limit", "messages"})
        ->setTooltip("Maximum number of messages retained in memory for the "
                     "Firehose channel.")
        ->addTo(layout);

    SettingWidget::intInput("Batch update interval", s.firehoseBatchIntervalMs,
                            {.min = 50,
                             .max = 2000,
                             .singleStep = 50,
                             .suffix = QStringLiteral(" ms")})
        ->addKeywords({"firehose", "batch", "interval", "render"})
        ->setTooltip("Interval for processing and rendering queued messages in "
                     "batches for smooth performance.")
        ->addTo(layout);

    layout.addTitle("Firehose Endpoints");
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

    layout.addStretch();
}

}  // namespace chatterino
