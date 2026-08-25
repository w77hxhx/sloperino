// SPDX-License-Identifier: MIT
// Dialog for the secondary "Limerino Extra Features" auth.
// Deliberately a NEW dialog; the stock LoginDialog is untouched.

#include "widgets/dialogs/LimerinoAuthDialog.hpp"

#include "providers/limerino/LimerinoAuth.hpp"
#include "util/Clipboard.hpp"
#include "util/IncognitoBrowser.hpp"

#include <QDateTime>
#include <QDesktopServices>
#include <QFontDatabase>
#include <QHeaderView>
#include <QLabel>
#include <QMessageBox>
#include <QPushButton>
#include <QTabWidget>
#include <QTableWidget>
#include <QUrl>
#include <QVBoxLayout>

namespace chatterino {

namespace {

QString channelListText(const LimerinoAuth::LimerinoAuthAccount &account)
{
    QStringList names;
    for (const auto &c : account.moderatedChannels)
    {
        names.append(c.displayName.isEmpty() ? c.login : c.displayName);
    }
    return names.join(QStringLiteral(", "));
}

}  // namespace

LimerinoAuthDialog::LimerinoAuthDialog(QWidget *parent)
    : BasePopup({BaseWindow::Flags::Dialog}, parent)
{
    this->setWindowTitle(QStringLiteral("Limerino - Extra features login"));
    this->resize(520, 480);
    this->setAttribute(Qt::WA_DeleteOnClose);

    auto *root = new QVBoxLayout(this);

    this->ui_.tabs = new QTabWidget(this);

    // ------------------------- Device Login tab -------------------------
    auto *devicePage = new QWidget(this);
    auto *deviceLayout = new QVBoxLayout(devicePage);

    auto *deviceInfo = new QLabel(
        QStringLiteral(
            "Sign in with a second, elevated authorization used only for "
            "extra features (moderation, rewards, chat settings). It is "
            "stored separately from your main account."),
        devicePage);
    deviceInfo->setWordWrap(true);
    deviceLayout->addWidget(deviceInfo);

    this->ui_.deviceStatus = new QLabel(devicePage);
    this->ui_.deviceStatus->setWordWrap(true);
    deviceLayout->addWidget(this->ui_.deviceStatus);

    this->ui_.deviceCode = new QLabel(devicePage);
    QFont codeFont = QFontDatabase::systemFont(QFontDatabase::FixedFont);
    codeFont.setPointSize(14);
    this->ui_.deviceCode->setFont(codeFont);
    this->ui_.deviceCode->setTextInteractionFlags(Qt::TextSelectableByMouse);
    deviceLayout->addWidget(this->ui_.deviceCode);

    // Verification link: a real clickable anchor (opens in an incognito
    // window so the main Twitch session is not disturbed) which stays
    // selectable/copyable, plus an explicit copy button.
    auto *linkRow = new QHBoxLayout;
    this->ui_.deviceLink = new QLabel(devicePage);
    this->ui_.deviceLink->setTextFormat(Qt::RichText);
    this->ui_.deviceLink->setTextInteractionFlags(Qt::TextBrowserInteraction);
    this->ui_.deviceLink->setWordWrap(true);
    this->ui_.deviceLink->setOpenExternalLinks(false);
    this->ui_.deviceLinkCopy =
        new QPushButton(QStringLiteral("Copy link"), devicePage);
    linkRow->addWidget(this->ui_.deviceLink, 1);
    linkRow->addWidget(this->ui_.deviceLinkCopy);
    deviceLayout->addLayout(linkRow);
    this->ui_.deviceLink->setVisible(false);
    this->ui_.deviceLinkCopy->setVisible(false);

    // Unambiguous success/failure surface (name + when + status).
    this->ui_.deviceResult = new QLabel(devicePage);
    this->ui_.deviceResult->setWordWrap(true);
    deviceLayout->addWidget(this->ui_.deviceResult);

    auto *deviceButtons = new QHBoxLayout;
    this->ui_.deviceStart = new QPushButton(QStringLiteral("Generate auth"),
                                            devicePage);
    this->ui_.deviceCancel = new QPushButton(QStringLiteral("Cancel"),
                                             devicePage);
    this->ui_.deviceCancel->setEnabled(false);
    deviceButtons->addWidget(this->ui_.deviceStart);
    deviceButtons->addWidget(this->ui_.deviceCancel);
    deviceButtons->addStretch(1);
    deviceLayout->addLayout(deviceButtons);
    deviceLayout->addStretch(1);

    // --------------------------- Accounts tab ---------------------------
    auto *accountsPage = new QWidget(this);
    auto *accountsLayout = new QVBoxLayout(accountsPage);

    this->ui_.accountsTable = new QTableWidget(0, 4, accountsPage);
    this->ui_.accountsTable->setHorizontalHeaderLabels(
        {QStringLiteral("Account"), QStringLiteral("Extra-feature channels"),
         QStringLiteral("Status"), QStringLiteral("Remove")});
    this->ui_.accountsTable->horizontalHeader()->setSectionResizeMode(
        0, QHeaderView::ResizeToContents);
    this->ui_.accountsTable->horizontalHeader()->setSectionResizeMode(
        1, QHeaderView::Stretch);
    this->ui_.accountsTable->horizontalHeader()->setSectionResizeMode(
        2, QHeaderView::ResizeToContents);
    this->ui_.accountsTable->verticalHeader()->setVisible(false);
    this->ui_.accountsTable->setEditTriggers(QAbstractItemView::NoEditTriggers);
    this->ui_.accountsTable->setSelectionMode(QAbstractItemView::NoSelection);
    this->ui_.accountsTable->setFocusPolicy(Qt::NoFocus);
    this->ui_.accountsTable->setAlternatingRowColors(true);
    accountsLayout->addWidget(this->ui_.accountsTable, 1);

    auto *accountsFooter = new QHBoxLayout;
    this->ui_.accountsSummary = new QLabel(accountsPage);
    auto *refreshButton =
        new QPushButton(QStringLiteral("Re-check accounts"), accountsPage);
    accountsFooter->addWidget(this->ui_.accountsSummary, 1);
    accountsFooter->addWidget(refreshButton);
    accountsLayout->addLayout(accountsFooter);

    this->ui_.tabs->addTab(devicePage, QStringLiteral("Device Login"));
    this->ui_.tabs->addTab(accountsPage, QStringLiteral("Accounts"));
    root->addWidget(this->ui_.tabs);

    // ------------------------------- logic ------------------------------

    this->deviceLogin_ = new LimerinoAuth::DeviceLogin(this);

    QObject::connect(
        this->deviceLogin_, &LimerinoAuth::DeviceLogin::statusChanged, this,
        [this](const LimerinoAuth::DeviceLogin::Status &status) {
            this->setDeviceStatusText(status);
        });

    QObject::connect(this->ui_.deviceStart, &QPushButton::clicked, this, [this] {
        // Snapshot the account universe so rebuildAccountsTable() can spot
        // the account this attempt adds and name it in the success line.
        this->knownUserIds_.clear();
        for (const auto &a : LimerinoAuth::accounts())
        {
            this->knownUserIds_.append(a.userId);
        }
        this->newAccountUserId_.clear();
        this->ui_.deviceResult->clear();
        this->deviceLogin_->start();
        this->ui_.deviceCancel->setEnabled(true);
    });
    QObject::connect(this->ui_.deviceCancel, &QPushButton::clicked, this,
                     [this] {
                         this->deviceLogin_->cancel();
                         this->ui_.deviceCancel->setEnabled(false);
                     });

    QObject::connect(this->ui_.deviceLink, &QLabel::linkActivated, this,
                     [](const QString &url) {
                         if (!openLinkIncognito(url))
                         {
                             QDesktopServices::openUrl(QUrl(url));
                         }
                     });
    QObject::connect(this->ui_.deviceLinkCopy, &QPushButton::clicked, this,
                     [this] {
                         if (!this->currentVerificationUri_.isEmpty())
                         {
                             crossPlatformCopy(this->currentVerificationUri_);
                         }
                     });

    QObject::connect(refreshButton, &QPushButton::clicked, this, [this] {
        this->ui_.accountsSummary->setText(QStringLiteral("Checking..."));
        LimerinoAuth::refreshAccounts(
            [this](const LimerinoAuth::LimerinoAuthRefreshResult &r) {
                QString text = QStringLiteral("%1 valid, %2 invalid of %3 "
                                              "accounts; extra features in %4 "
                                              "channels")
                                   .arg(r.valid)
                                   .arg(r.invalid)
                                   .arg(r.total)
                                   .arg(r.moderatedChannels);
                if (!r.errors.isEmpty())
                {
                    text += QStringLiteral(" - ") + r.errors.join("; ");
                }
                this->ui_.accountsSummary->setText(text);
            });
    });

    this->managedConnections_.managedConnect(
        LimerinoAuth::accountsChanged,
        [this] { this->rebuildAccountsTable(); });

    this->setDeviceStatusText(this->deviceLogin_->status());
    this->rebuildAccountsTable();
}

void LimerinoAuthDialog::setDeviceStatusText(
    const LimerinoAuth::DeviceLogin::Status &status)
{
    using State = LimerinoAuth::DeviceLogin::State;

    switch (status.state)
    {
        case State::WaitingForUser: {
            // Required deviation from Moltorino: we copy the user code AND
            // say we did, in the same action.
            crossPlatformCopy(status.userCode);
            this->ui_.deviceStatus->setText(
                status.message +
                QStringLiteral(" The code was copied to your clipboard."));
            this->ui_.deviceCode->setText(status.userCode);

            this->currentVerificationUri_ = status.verificationUri;
            const QString escaped = status.verificationUri.toHtmlEscaped();
            this->ui_.deviceLink->setText(
                QStringLiteral("<a href=\"%1\">%1</a>").arg(escaped));
            this->ui_.deviceLink->setVisible(true);
            this->ui_.deviceLinkCopy->setVisible(true);

            this->ui_.deviceStart->setText(QStringLiteral("Generating…"));
            this->ui_.deviceStart->setEnabled(false);
            break;
        }
        case State::RequestingCode:
            this->ui_.deviceStatus->setText(status.message);
            this->ui_.deviceLink->clear();
            this->ui_.deviceLink->setVisible(false);
            this->ui_.deviceLinkCopy->setVisible(false);
            this->ui_.deviceStart->setText(QStringLiteral("Generating…"));
            this->ui_.deviceStart->setEnabled(false);
            break;
        case State::Authorized:
            this->ui_.deviceStatus->setText(status.message);
            this->ui_.deviceCode->clear();
            this->ui_.deviceLink->clear();
            this->ui_.deviceLink->setVisible(false);
            this->ui_.deviceLinkCopy->setVisible(false);
            this->ui_.deviceStart->setText(QStringLiteral("Generate another"));
            this->ui_.deviceStart->setEnabled(true);
            this->ui_.deviceCancel->setEnabled(false);
            this->updateDeviceResult();
            break;
        case State::Idle:
            this->ui_.deviceStatus->setText(QStringLiteral("Not signed in."));
            this->ui_.deviceCode->clear();
            this->ui_.deviceLink->clear();
            this->ui_.deviceLink->setVisible(false);
            this->ui_.deviceLinkCopy->setVisible(false);
            this->ui_.deviceStart->setText(QStringLiteral("Generate auth"));
            this->ui_.deviceStart->setEnabled(true);
            this->ui_.deviceCancel->setEnabled(false);
            break;
        default:
            // Denied, Expired, Failed - message only, reset the button.
            this->ui_.deviceStatus->setText(status.message);
            this->ui_.deviceLink->setVisible(false);
            this->ui_.deviceLinkCopy->setVisible(false);
            this->ui_.deviceStart->setText(QStringLiteral("Generate auth"));
            this->ui_.deviceStart->setEnabled(true);
            this->ui_.deviceCancel->setEnabled(false);
            break;
    }

    if (status.secondsRemaining > 0 &&
        status.state == State::WaitingForUser)
    {
        this->ui_.deviceStatus->setText(
            this->ui_.deviceStatus->text() +
            QStringLiteral(" (%1 s left)").arg(status.secondsRemaining));
    }
}

// Renders the "Signed in as NAME — generated TIME. Status: …" line on the
// Device page once the freshly added account shows up in the store, and keeps
// its status text in sync with validation.
void LimerinoAuthDialog::updateDeviceResult()
{
    using State = LimerinoAuth::DeviceLogin::State;
    if (this->deviceLogin_ == nullptr ||
        this->deviceLogin_->status().state != State::Authorized)
    {
        return;
    }

    const auto all = LimerinoAuth::accounts();
    if (this->newAccountUserId_.isEmpty())
    {
        for (const auto &a : all)
        {
            if (!this->knownUserIds_.contains(a.userId))
            {
                this->newAccountUserId_ = a.userId;
                this->generatedAt_ = QDateTime::currentDateTime();
                break;
            }
        }
    }
    if (this->newAccountUserId_.isEmpty())
    {
        return;
    }

    for (const auto &a : all)
    {
        if (a.userId == this->newAccountUserId_)
        {
            const QString name =
                a.displayName.isEmpty()
                    ? (a.login.isEmpty() ? a.userId : a.login)
                    : a.displayName;
            this->ui_.deviceResult->setText(
                QStringLiteral("Signed in as %1 — generated %2. Status: %3.")
                    .arg(name)
                    .arg(this->generatedAt_.toString(
                        QStringLiteral("yyyy-MM-dd hh:mm:ss")))
                    .arg(a.valid ? QStringLiteral("valid")
                                 : QStringLiteral("validating…")));
            return;
        }
    }
}

void LimerinoAuthDialog::rebuildAccountsTable()
{
    const auto accounts = LimerinoAuth::accounts();
    this->ui_.accountsTable->setRowCount(0);
    this->ui_.accountsTable->setRowCount(int(accounts.size()));

    int row = 0;
    for (const auto &account : accounts)
    {
        const QString title = account.displayName.isEmpty()
                                  ? account.login
                                  : account.displayName;
        auto *accountItem =
            new QTableWidgetItem(QStringLiteral("%1 (ID %2)")
                                     .arg(title, account.userId));
        auto *channelsItem =
            new QTableWidgetItem(channelListText(account));

        QString statusText =
            account.valid
                ? QStringLiteral("valid, %1 scopes, checked %2")
                      .arg(account.scopes.size())
                      .arg(account.lastValidatedAt.isValid()
                               ? account.lastValidatedAt.toString(
                                     QStringLiteral("yyyy-MM-dd hh:mm"))
                               : QStringLiteral("never"))
                : QStringLiteral("invalid: %1").arg(account.lastError);
        auto *statusItem = new QTableWidgetItem(statusText);

        this->ui_.accountsTable->setItem(row, 0, accountItem);
        this->ui_.accountsTable->setItem(row, 1, channelsItem);
        this->ui_.accountsTable->setItem(row, 2, statusItem);

        const QString userId = account.userId;
        auto *removeButton = new QPushButton(QStringLiteral("Remove"),
                                             this->ui_.accountsTable);
        QObject::connect(removeButton, &QPushButton::clicked, this,
                         [this, userId, title] {
                             const auto answer = QMessageBox::question(
                                 this, QStringLiteral("Remove account"),
                                 QStringLiteral("Remove the extra-features "
                                                "login for %1?")
                                     .arg(title));
                             if (answer == QMessageBox::Yes)
                             {
                                 LimerinoAuth::removeAccount(userId);
                             }
                         });
        this->ui_.accountsTable->setCellWidget(row, 3, removeButton);
        ++row;
    }

    const auto s = LimerinoAuth::summary();
    this->ui_.accountsSummary->setText(
        QStringLiteral("%1 accounts (%2 valid). Extra features available in "
                       "%3 channels.")
            .arg(s.accountCount)
            .arg(s.validAccountCount)
            .arg(s.moderatedChannelCount));

    this->updateDeviceResult();
}

}  // namespace chatterino
