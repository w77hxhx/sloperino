#include "limerino/LimerinoPage.hpp"

#include "limerino/LimerinoWikiWidget.hpp"
#include "limerino/PubSubEventsChannel.hpp"
#include "providers/limerino/LimerinoAuth.hpp"
#include "providers/limerino/pubsub/LimerinoPubSubController.hpp"
#include "singletons/Settings.hpp"
#include "widgets/dialogs/LimerinoAuthDialog.hpp"
#include "widgets/dialogs/limerino/LimerinoThemeDialog.hpp"
#include "widgets/settingspages/GeneralPageView.hpp"
#include "widgets/settingspages/SettingWidget.hpp"

#include <QFrame>
#include <QHBoxLayout>
#include <QVBoxLayout>

namespace chatterino {

LimerinoPage::LimerinoPage()
{
    auto *y = new QVBoxLayout;
    auto *x = new QHBoxLayout;
    auto *view = GeneralPageView::withNavigation(this);
    this->view_ = view;
    x->addWidget(view);
    auto *z = new QFrame;
    z->setLayout(x);
    y->addWidget(z);
    this->setLayout(y);

    this->initLayout(*view);
}

bool LimerinoPage::filterElements(const QString &query)
{
    if (this->view_)
    {
        return this->view_->filterElements(query) || query.isEmpty();
    }
    else
    {
        return false;
    }
}

void LimerinoPage::initLayout(GeneralPageView &layout)
{
    // Feature wiki (top of page). Renders entirely from the catalog in
    // LimerinoFeatures.cpp; tests/src/LimerinoFeatures.cpp keeps it honest.
    layout.addTitle("Limerino features");
    layout.addDescription(QStringLiteral(
        "What this fork adds, and every way to reach it. Generated from the "
        "feature catalog - new features add their entry (including non-command "
        "access paths) in the same commit as their code."));
    layout.addWidget(new limerino::LimerinoWikiWidget());

    layout.addTitle("Limerino");
    layout.addDescription(QStringLiteral(
        R"(<a href="https://github.com/lagx/Limerino">Limerino</a> is a Chatterino fork built on top of technorino.)"));
    layout.addDescription(QStringLiteral(
        "Limerino-specific settings and features will live on this page."));

    layout.addTitle("Theme creator");
    layout.addDescription(QStringLiteral(
        "Recolor a real Chatterino theme (Dark.json / Light.json / Black.json "
        "/ White.json, or a recently saved custom theme). Only the four seed "
        "colours are replaced; every other leaf stays from the base file. "
        "Live-previews while you edit; Apply installs it into Themes/ and "
        "selects it immediately."));
    layout.addButton(QStringLiteral("Create theme..."), [this] {
        auto *dialog = new limerino::LimerinoThemeDialog(this);
        dialog->show();
    });

    layout.addTitle("Extra features");
    layout.addDescription(QStringLiteral(
        "Options for extra features (moderation, rewards, chat settings) "
        "that need a second, elevated sign-in. Stored separately from your "
        "main account."));
    this->authSummaryLabel_ = layout.addDescription(QString());
    layout.addButton(QStringLiteral("Manage extra-features login..."),
                     [this] {
                         auto *dialog = new LimerinoAuthDialog(this);
                         dialog->show();
                     });

    layout.addTitle("Events");
    layout.addDescription(QStringLiteral(
        "Live event notifications from your extra-features sign-in "
        "(moderation, predictions, channel points, raids). Tokens come only "
        "from the extra-features sign-in above; they are checked once at "
        "startup and whenever this page is opened."));
    this->pubsubSummaryLabel_ = layout.addDescription(QString());
    this->pubsubDetailLabel_ = layout.addDescription(QString());
    SettingWidget::checkbox(
        QStringLiteral("Automatically acknowledge chat warnings"),
        getSettings()->limerinoAutoAcknowledgeChatWarnings)
        ->addTo(layout);
    SettingWidget::checkbox(
        QStringLiteral("Deduplicate identical events in /events"),
        getSettings()->limerinoPubSubDedupeEnabled)
        ->addTo(layout);
    layout.addButton(QStringLiteral("Retry failed listens"), [] {
        limerino::getPubSubController()->retryFailed();
    });
    layout.addButton(QStringLiteral("Open events channel"), [] {
        limerino::openPubSubEventsChannelTab();
    });

    layout.addDescription(QStringLiteral(
        "Auto actions live on their own settings tab "
        "(Settings > Auto Actions)."));

    layout.addDescription(QStringLiteral(
        "Paste host used by list commands (/listfollows, /modlist, ...): "
        "your generated list output is uploaded publicly to this "
        "third-party service. Change only if you trust it."));
    SettingWidget::lineEdit(
        QStringLiteral("Paste host"), getSettings()->limerinoPasteHost,
        QStringLiteral("List output is uploaded to this service "
                       "(defaults to https://h.potat.app)"))
        ->addTo(layout);

    this->managedConnections_.managedConnect(
        LimerinoAuth::accountsChanged,
        [this] { this->rebuildAuthSummary(); });
    this->managedConnections_.managedConnect(
        LimerinoAuth::accountsChanged,
        [this] { this->rebuildPubSubDiagnostics(); });
    this->managedConnections_.managedConnect(
        limerino::getPubSubController()->diagChanged,
        [this] { this->rebuildPubSubDiagnostics(); });
    this->rebuildAuthSummary();
    this->rebuildPubSubDiagnostics();

    layout.addStretch();
}

void LimerinoPage::onShow()
{
    // Opening the Limerino tab always refreshes the auth cache (validity +
    // moderated-channel lists may have drifted since the last check).
    LimerinoAuth::refreshAccounts();
}

void LimerinoPage::rebuildAuthSummary()
{
    if (this->authSummaryLabel_ == nullptr)
    {
        return;
    }
    const auto accounts = LimerinoAuth::accounts();
    const auto s = LimerinoAuth::summary();
    QString text;
    if (accounts.isEmpty())
    {
        text = QStringLiteral("No extra-features accounts signed in.");
    }
    else
    {
        text = QStringLiteral("Signed in to %1 account(s). Extra features "
                              "available in %2 channels.")
                   .arg(s.accountCount)
                   .arg(s.moderatedChannelCount);
        if (s.invalidAccountCount > 0)
        {
            text += QStringLiteral(" (%1 account(s) need attention, see "
                                   "Manage extra-features login)")
                        .arg(s.invalidAccountCount);
        }
        for (const auto &a : accounts)
        {
            text += QStringLiteral("\n%1: %2, scopes: %3, checked: %4, "
                                   "channels: %5")
                        .arg(a.displayName.isEmpty() ? a.login : a.displayName)
                        .arg(a.valid ? QStringLiteral("valid")
                                     : QStringLiteral("invalid: ") + a.lastError)
                        .arg(a.scopes.isEmpty()
                                 ? QStringLiteral("(not validated yet)")
                                 : QString::number(a.scopes.size()))
                        .arg(a.lastValidatedAt.isValid()
                                 ? a.lastValidatedAt.toString(
                                       QStringLiteral("yyyy-MM-dd hh:mm"))
                                 : QStringLiteral("never"))
                        .arg(a.moderatedChannels.size());
        }
    }
    this->authSummaryLabel_->setText(text);
}

void LimerinoPage::rebuildPubSubDiagnostics()
{
    if (this->pubsubSummaryLabel_ == nullptr)
    {
        return;
    }
    auto *controller = limerino::getPubSubController();
    const auto snapshot = controller->diagSnapshot();
    const auto &t = snapshot.transport;

    this->pubsubSummaryLabel_->setText(QStringLiteral(
        "Connections: %1 open (%2 opened, %3 failed) - notifications: %4\n"
        "Topics: %5 active, %6 pending, %7 retrying, %8 failed, %9 blocked "
        "(need extra-features sign-in)\n"
        "Listens: %10 confirmed, %11 failed - auth failures: %12 - "
        "reconnects: %13 - missed keepalives: %14")
            .arg(t.connections)
            .arg(t.connectionsOpened)
            .arg(t.connectionsFailed)
            .arg(t.notificationsReceived)
            .arg(snapshot.topicsActive)
            .arg(snapshot.topicsPending)
            .arg(snapshot.topicsRetrying)
            .arg(snapshot.topicsFailed)
            .arg(snapshot.topicsBlocked)
            .arg(t.subscribeResponses)
            .arg(t.failedSubscribeResponses)
            .arg(t.authFailures)
            .arg(t.reconnectsReceived)
            .arg(t.keepalivesMissed));

    QString detail;
    if (!snapshot.lastError.isEmpty())
    {
        detail = QStringLiteral("Last error: %1 (%2)")
                     .arg(snapshot.lastError, snapshot.lastErrorTopic);
    }
    if (snapshot.topicsFailed > 0)
    {
        if (!detail.isEmpty())
        {
            detail += QLatin1Char('\n');
        }
        detail += QStringLiteral(
            "%1 topic(s) gave up after repeated failures - fix the sign-in "
            "above, then press \"Retry failed listens\".")
                      .arg(snapshot.topicsFailed);
    }
    this->pubsubDetailLabel_->setText(detail);
}

}  // namespace chatterino

