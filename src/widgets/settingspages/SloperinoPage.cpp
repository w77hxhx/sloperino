// SPDX-FileCopyrightText: 2026 Contributors to Chatterino <https://chatterino.com>
//
// SPDX-License-Identifier: MIT

#include "widgets/settingspages/SloperinoPage.hpp"

#include "Application.hpp"
#include "common/network/NetworkRequest.hpp"
#include "common/network/NetworkResult.hpp"
#include "controllers/aliases/EmoteAlias.hpp"
#include "controllers/aliases/EmoteAliasesModel.hpp"
#include "providers/firehose/FirehoseManager.hpp"
#include "singletons/Settings.hpp"
#include "util/Clipboard.hpp"
#include "widgets/BaseWidget.hpp"
#include "widgets/dialogs/MoltorinoAuthDialog.hpp"
#include "widgets/helper/EditableModelView.hpp"
#include "widgets/settingspages/GeneralPageView.hpp"
#include "widgets/settingspages/SettingWidget.hpp"

#include <QCheckBox>
#include <QDesktopServices>
#include <QDialog>
#include <QDialogButtonBox>
#include <QFrame>
#include <QHBoxLayout>
#include <QHeaderView>
#include <QJsonArray>
#include <QJsonDocument>
#include <QJsonObject>
#include <QLabel>
#include <QLineEdit>
#include <QMessageBox>
#include <QPointer>
#include <QPushButton>
#include <QTableView>
#include <QTimer>
#include <QUrl>
#include <QVBoxLayout>

namespace {

using chatterino::NetworkRequest;
using chatterino::NetworkRequestType;
using chatterino::NetworkResult;

QString decodeJwtUserId(const QString &jwt)
{
    const auto parts = jwt.split('.');
    if (parts.size() < 2)
    {
        return {};
    }

    auto payload = parts[1].toUtf8();
    auto json = QByteArray::fromBase64(
        payload,
        QByteArray::Base64UrlEncoding | QByteArray::OmitTrailingEquals);
    auto doc = QJsonDocument::fromJson(json);
    if (doc.isObject())
    {
        const auto sub = doc.object().value("sub").toString().trimmed();
        if (!sub.isEmpty())
        {
            return sub;
        }
    }

    payload.replace('-', '+').replace('_', '/');
    while (payload.size() % 4 != 0)
    {
        payload.append('=');
    }

    const auto fallbackDoc =
        QJsonDocument::fromJson(QByteArray::fromBase64(payload));
    if (fallbackDoc.isObject())
    {
        return fallbackDoc.object().value("sub").toString().trimmed();
    }

    return {};
}

void validateSeventvToken(
    const QString &token,
    std::function<void(QString userId, QString username, QString displayName)>
        onSuccess,
    std::function<void(QString error)> onError)
{
    const auto trimmed = token.trimmed();
    if (trimmed.isEmpty())
    {
        if (onError)
        {
            onError("No token provided");
        }
        return;
    }

    QJsonObject gqlQuery;
    gqlQuery.insert(
        QStringLiteral("query"),
        QStringLiteral("{ user: actor { id username display_name } }"));

    NetworkRequest(QUrl(QStringLiteral("https://7tv.io/v3/gql")),
                   NetworkRequestType::Post)
        .header("Authorization",
                QStringLiteral("Bearer %1").arg(trimmed).toUtf8())
        .header("Content-Type", "application/json")
        .header("Accept", "application/json")
        .header("User-Agent", "Chatterino")
        .json(gqlQuery)
        .timeout(15000)
        .onSuccess([onSuccess, onError, trimmed](const NetworkResult &res) {
            const auto obj = res.parseJson();
            const auto dataObj = obj.value("data").toObject();
            const auto userObj = dataObj.value("user").toObject();

            auto id = userObj.value("id").toString().trimmed();
            auto username = userObj.value("username").toString().trimmed();
            auto displayName =
                userObj.value("display_name").toString().trimmed();

            if (id.isEmpty())
            {
                const auto jwtId = decodeJwtUserId(trimmed);
                if (!jwtId.isEmpty())
                {
                    NetworkRequest(QUrl(QStringLiteral("https://7tv.io/v3/users/%1")
                                            .arg(jwtId)),
                                   NetworkRequestType::Get)
                        .header("Authorization",
                                QStringLiteral("Bearer %1").arg(trimmed).toUtf8())
                        .header("Accept", "application/json")
                        .header("User-Agent", "Chatterino")
                        .timeout(15000)
                        .onSuccess([onSuccess,
                                    onError](const NetworkResult &restRes) {
                            const auto restObj = restRes.parseJson();
                            const auto rId =
                                restObj.value("id").toString().trimmed();
                            auto rUsername =
                                restObj.value("username").toString().trimmed();
                            const auto rDisplayName =
                                restObj.value("display_name")
                                    .toString()
                                    .trimmed();

                            if (rUsername.isEmpty() && !rDisplayName.isEmpty())
                            {
                                rUsername = rDisplayName;
                            }
                            if (rId.isEmpty())
                            {
                                if (onError)
                                {
                                    onError("7TV API did not return user ID");
                                }
                                return;
                            }
                            if (onSuccess)
                            {
                                onSuccess(rId, rUsername, rDisplayName);
                            }
                        })
                        .onError([onError](const NetworkResult &restRes) {
                            if (!onError)
                            {
                                return;
                            }
                            if (restRes.status() == 401 ||
                                restRes.status() == 403)
                            {
                                onError("Invalid or expired 7TV token");
                                return;
                            }
                            onError(restRes.formatError());
                        })
                        .execute();
                    return;
                }

                if (onError)
                {
                    const auto errorsArr = obj.value("errors").toArray();
                    if (!errorsArr.isEmpty())
                    {
                        const auto firstErr = errorsArr.at(0)
                                                  .toObject()
                                                  .value("message")
                                                  .toString();
                        if (!firstErr.isEmpty())
                        {
                            onError(firstErr);
                            return;
                        }
                    }
                    onError("Failed to authenticate with 7TV");
                }
                return;
            }

            if (username.isEmpty() && !displayName.isEmpty())
            {
                username = displayName;
            }

            if (onSuccess)
            {
                onSuccess(id, username, displayName);
            }
        })
        .onError([onSuccess, onError, trimmed](const NetworkResult &res) {
            const auto jwtId = decodeJwtUserId(trimmed);
            if (!jwtId.isEmpty() && res.status() != 401 && res.status() != 403)
            {
                NetworkRequest(
                    QUrl(QStringLiteral("https://7tv.io/v3/users/%1").arg(jwtId)),
                    NetworkRequestType::Get)
                    .header("Authorization",
                            QStringLiteral("Bearer %1").arg(trimmed).toUtf8())
                    .header("Accept", "application/json")
                    .header("User-Agent", "Chatterino")
                    .timeout(15000)
                    .onSuccess([onSuccess,
                                onError](const NetworkResult &restRes) {
                        const auto restObj = restRes.parseJson();
                        const auto rId =
                            restObj.value("id").toString().trimmed();
                        auto rUsername =
                            restObj.value("username").toString().trimmed();
                        const auto rDisplayName =
                            restObj.value("display_name").toString().trimmed();

                        if (rUsername.isEmpty() && !rDisplayName.isEmpty())
                        {
                            rUsername = rDisplayName;
                        }
                        if (!rId.isEmpty() && onSuccess)
                        {
                            onSuccess(rId, rUsername, rDisplayName);
                            return;
                        }
                        if (onError)
                        {
                            onError("7TV API did not return user ID");
                        }
                    })
                    .onError([onError](const NetworkResult &restRes) {
                        if (!onError)
                        {
                            return;
                        }
                        if (restRes.status() == 401 || restRes.status() == 403)
                        {
                            onError("Invalid or expired 7TV token");
                            return;
                        }
                        onError(restRes.formatError());
                    })
                    .execute();
                return;
            }

            if (!onError)
            {
                return;
            }
            if (res.status() == 401 || res.status() == 403)
            {
                onError("Invalid or expired 7TV token");
                return;
            }
            const auto body = QString::fromUtf8(res.getData()).trimmed();
            if (!body.isEmpty())
            {
                const auto json = res.parseJson();
                const auto errorMsg = json.value("error").toString();
                if (!errorMsg.isEmpty())
                {
                    onError(errorMsg);
                    return;
                }
                onError(QString("7TV API error: %1").arg(body.left(120)));
                return;
            }
            onError(res.formatError());
        })
        .execute();
}

}  // namespace


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

    // 0. Authentication Category (7TV)
    layout.addTitle("Authentication");
    layout.addDescription("Manage 7TV authentication and accounts.");

    auto *authFrame = new QFrame;
    auto *authLayout = new QVBoxLayout(authFrame);
    authLayout->setContentsMargins(0, 0, 0, 0);
    authLayout->setSpacing(6);

    this->seventvStatusLabel_ = new QLabel(authFrame);
    this->seventvStatusLabel_->setStyleSheet(
        "QLabel { font-weight: 600; font-size: 13px; }");
    authLayout->addWidget(this->seventvStatusLabel_);

    this->seventvDetailsLabel_ = new QLabel(authFrame);
    this->seventvDetailsLabel_->setWordWrap(true);
    this->seventvDetailsLabel_->setStyleSheet(
        "QLabel { color: #9aa0a6; font-size: 12px; }");
    authLayout->addWidget(this->seventvDetailsLabel_);

    auto *authRow = new QHBoxLayout;
    this->addSeventvBtn_ = new QPushButton("Add Token", authFrame);
    this->refreshSeventvBtn_ = new QPushButton("Refresh Account", authFrame);
    this->removeSeventvBtn_ = new QPushButton("Log Out", authFrame);

    QObject::connect(this->addSeventvBtn_, &QPushButton::clicked, this, [this] {
        this->openSeventvAuthDialog();
    });
    QObject::connect(this->refreshSeventvBtn_, &QPushButton::clicked, this,
                     [this] {
                         this->refreshSeventvAccount();
                     });
    QObject::connect(this->removeSeventvBtn_, &QPushButton::clicked, this,
                     [this] {
                         this->removeSeventvAccount();
                     });

    authRow->addWidget(this->addSeventvBtn_);
    authRow->addWidget(this->refreshSeventvBtn_);
    authRow->addWidget(this->removeSeventvBtn_);
    authRow->addStretch(1);
    authLayout->addLayout(authRow);

    layout.addWidget(authFrame);
    this->updateSeventvStatus();

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

void SloperinoPage::openSeventvAuthDialog()
{
    QDialog dialog(this);
    dialog.setWindowTitle("7TV Authentication");
    dialog.setMinimumWidth(440);

    auto *layout = new QVBoxLayout(&dialog);
    layout->setSpacing(10);

    auto *desc =
        new QLabel("Paste your 7TV personal authentication token below.\n"
                   "You can retrieve it from 7tv.app using the helper script:",
                   &dialog);
    desc->setWordWrap(true);
    layout->addWidget(desc);

    auto *scriptBtn = new QPushButton("Copy Script & Open 7tv.app", &dialog);
    scriptBtn->setToolTip(
        "Copies token extraction command and opens 7TV in your browser");
    QObject::connect(scriptBtn, &QPushButton::clicked, &dialog, [&dialog] {
        const auto script = QStringLiteral(
            "const token = localStorage.getItem('7tv-token');\n"
            "if (token) {\n"
            "    const input = document.createElement('input');\n"
            "    input.value = token;\n"
            "    document.body.appendChild(input);\n"
            "    input.select();\n"
            "    document.execCommand('copy');\n"
            "    document.body.removeChild(input);\n"
            "    console.log('Token copied, return to Sloperino', token);\n"
            "} else {\n"
            "    console.log('Token not found');\n"
            "}");
        crossPlatformCopy(script);
        QDesktopServices::openUrl(QUrl("https://7tv.app/store"));

        QMessageBox box(&dialog);
        box.setWindowTitle("Script Copied");
        box.setIcon(QMessageBox::Information);
        box.setText(
            "1. 7tv.app opened in your browser.\n"
            "2. Press F12, go to Console, paste the script and hit Enter.\n"
            "3. Return here and click 'Paste'.");
        box.exec();
    });
    layout->addWidget(scriptBtn);

    auto *tokenRow = new QHBoxLayout();
    auto *tokenInput = new QLineEdit(&dialog);
    tokenInput->setPlaceholderText("Paste 7TV token here...");
    tokenInput->setEchoMode(QLineEdit::Password);
    tokenInput->setText(getSettings()->seventvToken.getValue().trimmed());
    tokenRow->addWidget(tokenInput);

    auto *pasteBtn = new QPushButton("Paste", &dialog);
    QObject::connect(pasteBtn, &QPushButton::clicked, tokenInput, [tokenInput] {
        tokenInput->setText(getClipboardText().trimmed());
    });
    tokenRow->addWidget(pasteBtn);
    layout->addLayout(tokenRow);

    auto *dialogStatus = new QLabel(&dialog);
    dialogStatus->setWordWrap(true);
    dialogStatus->hide();
    layout->addWidget(dialogStatus);

    auto *btnBox = new QDialogButtonBox(&dialog);
    auto *saveBtn =
        btnBox->addButton("Verify & Save", QDialogButtonBox::ActionRole);
    auto *cancelBtn = btnBox->addButton(QDialogButtonBox::Cancel);
    layout->addWidget(btnBox);

    QObject::connect(cancelBtn, &QPushButton::clicked, &dialog,
                     &QDialog::reject);

    const QPointer<QDialog> dialogPtr(&dialog);
    const QPointer<QLabel> statusPtr(dialogStatus);
    const QPointer<QPushButton> saveBtnPtr(saveBtn);

    QObject::connect(
        saveBtn, &QPushButton::clicked, &dialog,
        [this, dialogPtr, tokenInput, saveBtnPtr, statusPtr] {
            const auto token = tokenInput->text().trimmed();
            if (token.isEmpty())
            {
                if (statusPtr)
                {
                    statusPtr->setText("Please enter a 7TV token.");
                    statusPtr->setStyleSheet("QLabel { color: #f44336; }");
                    statusPtr->show();
                }
                return;
            }

            if (saveBtnPtr)
            {
                saveBtnPtr->setEnabled(false);
            }
            if (statusPtr)
            {
                statusPtr->setText("Verifying with 7TV API...");
                statusPtr->setStyleSheet("QLabel { color: #9aa0a6; }");
                statusPtr->show();
            }

            validateSeventvToken(
                token,
                [this, dialogPtr, token](
                    QString userId, QString username, QString /*displayName*/) {
                    getSettings()->seventvToken.setValue(token);
                    getSettings()->seventvUserId.setValue(userId);
                    getSettings()->seventvUsername.setValue(username);
                    this->updateSeventvStatus();
                    if (dialogPtr)
                    {
                        dialogPtr->accept();
                    }
                },
                [saveBtnPtr, statusPtr](QString error) {
                    if (saveBtnPtr)
                    {
                        saveBtnPtr->setEnabled(true);
                    }
                    if (statusPtr)
                    {
                        statusPtr->setText(
                            QString("Validation failed: %1").arg(error));
                        statusPtr->setStyleSheet("QLabel { color: #f44336; }");
                        statusPtr->show();
                    }
                });
        });

    dialog.exec();
}

void SloperinoPage::refreshSeventvAccount()
{
    const auto token = getSettings()->seventvToken.getValue().trimmed();
    if (token.isEmpty())
    {
        this->updateSeventvStatus();
        return;
    }

    if (this->seventvDetailsLabel_ != nullptr)
    {
        this->seventvDetailsLabel_->setText(
            "Refreshing account via 7TV API...");
    }

    validateSeventvToken(
        token,
        [this](QString userId, QString username, QString /*displayName*/) {
            getSettings()->seventvUserId.setValue(userId);
            getSettings()->seventvUsername.setValue(username);
            this->updateSeventvStatus();
        },
        [this](QString error) {
            if (this->seventvDetailsLabel_ != nullptr)
            {
                this->seventvDetailsLabel_->setText(
                    QString("7TV verification error: %1").arg(error));
            }
        });
}

void SloperinoPage::removeSeventvAccount()
{
    getSettings()->seventvToken.setValue(QString());
    getSettings()->seventvUserId.setValue(QString());
    getSettings()->seventvUsername.setValue(QString());
    this->updateSeventvStatus();
}

void SloperinoPage::updateSeventvStatus()
{
    const auto token = getSettings()->seventvToken.getValue().trimmed();
    const auto username = getSettings()->seventvUsername.getValue().trimmed();
    const auto userId = getSettings()->seventvUserId.getValue().trimmed();

    const bool isLoggedIn = !token.isEmpty();

    if (this->seventvStatusLabel_ != nullptr)
    {
        if (isLoggedIn)
        {
            const auto displayName =
                !username.isEmpty()
                    ? username
                    : (!userId.isEmpty() ? userId
                                         : QStringLiteral("Connected"));
            this->seventvStatusLabel_->setText(
                QStringLiteral("Logged in as <b>%1</b> (7TV ID: %2)")
                    .arg(displayName, userId.isEmpty() ? "unknown" : userId));
        }
        else
        {
            this->seventvStatusLabel_->setText("Not logged in to 7TV.");
        }
    }

    if (this->seventvDetailsLabel_ != nullptr)
    {
        this->seventvDetailsLabel_->setText(
            isLoggedIn ? "7TV token is active. Cosmetics, badges, and paints "
                         "are synced."
                       : "Add your 7TV token to enable 7TV badges, paints, and "
                         "cosmetics.");
    }

    if (this->addSeventvBtn_ != nullptr)
    {
        this->addSeventvBtn_->setText(isLoggedIn ? "Change Token"
                                                 : "Add Token");
    }
    if (this->refreshSeventvBtn_ != nullptr)
    {
        this->refreshSeventvBtn_->setVisible(isLoggedIn);
    }
    if (this->removeSeventvBtn_ != nullptr)
    {
        this->removeSeventvBtn_->setVisible(isLoggedIn);
    }
}

}  // namespace chatterino
