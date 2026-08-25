// SPDX-License-Identifier: MIT

#include "widgets/dialogs/limerino/LimerinoCrossbanDialog.hpp"

#include "Application.hpp"
#include "controllers/accounts/AccountController.hpp"
#include "providers/limerino/crossban/CrossbanComments.hpp"
#include "providers/limerino/crossban/CrossbanStrike.hpp"
#include "providers/limerino/gql/LimerinoGql.hpp"
#include "providers/limerino/gql/PersistedQueries.hpp"
#include "providers/limerino/LimerinoApi.hpp"
#include "providers/limerino/LimerinoAuth.hpp"
#include "providers/twitch/TwitchAccount.hpp"
#include "providers/twitch/TwitchAccountManager.hpp"

#include <QAbstractItemView>
#include <QCheckBox>
#include <QComboBox>
#include <QCompleter>
#include <QHBoxLayout>
#include <QHeaderView>
#include <QInputDialog>
#include <QLabel>
#include <QLineEdit>
#include <QListWidget>
#include <QMessageBox>
#include <QPointer>
#include <QPushButton>
#include <QTabWidget>
#include <QTableWidget>
#include <QVBoxLayout>

namespace chatterino::limerino {

namespace gql = chatterino::LimerinoAuth::gql;

namespace {

constexpr int COL_CHECK = 0;
constexpr int COL_CHANNEL = 1;
constexpr int COL_STATUS = 2;
constexpr int COL_DETAIL = 3;
constexpr int COL_ACTIONS = 4;

QString primaryModeratorId()
{
    const auto user = getApp()->getAccounts()->twitch.getCurrent();
    if (!user || user->isAnon())
    {
        return {};
    }
    return user->getUserId();
}

}  // namespace

LimerinoCrossbanDialog::LimerinoCrossbanDialog(QString targetUserId,
                                               QString targetLogin,
                                               QString targetDisplayName,
                                               QWidget *parent)
    : BasePopup({BaseWindow::Flags::Dialog}, parent)
    , targetUserId_(std::move(targetUserId))
    , targetLogin_(std::move(targetLogin))
    , targetDisplayName_(std::move(targetDisplayName))
{
    this->setAttribute(Qt::WA_DeleteOnClose);
    const QString titleName = this->targetDisplayName_.isEmpty()
                                  ? this->targetLogin_
                                  : this->targetDisplayName_;
    this->setWindowTitle(
        QStringLiteral("Crossban - %1 (Limerino)").arg(titleName));
    this->resize(780, 520);
    this->presets_ = loadCrossbanPresets();
    this->buildUi();
    this->rebuildPresetCombo();
    this->rebuildPresetList();
    this->fillModeratedIntoInputCompleter();

    const QUuid last = lastCrossbanPresetId();
    int select = 0;
    for (int i = 0; i < this->presets_.size(); ++i)
    {
        if (this->presets_[i].id == last)
        {
            select = i;
            break;
        }
    }
    if (this->presetCombo_->count() > 0)
    {
        this->presetCombo_->setCurrentIndex(select);
    }
    this->loadSelectedPresetIntoTable();
}

void LimerinoCrossbanDialog::buildUi()
{
    auto *root = new QVBoxLayout(this);

    auto *header = new QLabel(
        QStringLiteral("%1 (%2) · id %3")
            .arg(this->targetDisplayName_.isEmpty() ? this->targetLogin_
                                                    : this->targetDisplayName_,
                 this->targetLogin_, this->targetUserId_),
        this);
    header->setWordWrap(true);
    root->addWidget(header);

    this->tabs_ = new QTabWidget(this);
    root->addWidget(this->tabs_, 1);

    // ----- Channels tab -----
    {
        auto *page = new QWidget(this);
        auto *lay = new QVBoxLayout(page);

        auto *top = new QHBoxLayout;
        top->addWidget(new QLabel(QStringLiteral("Preset"), page));
        this->presetCombo_ = new QComboBox(page);
        top->addWidget(this->presetCombo_, 1);
        auto *refreshBtn = new QPushButton(QStringLiteral("Refresh"), page);
        top->addWidget(refreshBtn);
        lay->addLayout(top);

        QObject::connect(this->presetCombo_,
                         QOverload<int>::of(&QComboBox::currentIndexChanged),
                         this, [this](int) {
                             this->loadSelectedPresetIntoTable();
                         });
        QObject::connect(refreshBtn, &QPushButton::clicked, this,
                         &LimerinoCrossbanDialog::refreshAllStrikes);

        auto *reasonRow = new QHBoxLayout;
        reasonRow->addWidget(new QLabel(QStringLiteral("Reason"), page));
        this->reasonEdit_ = new QLineEdit(page);
        this->reasonEdit_->setPlaceholderText(
            QStringLiteral("Shared ban / timeout reason"));
        reasonRow->addWidget(this->reasonEdit_, 1);
        lay->addLayout(reasonRow);

        this->table_ = new QTableWidget(0, 5, page);
        this->table_->setHorizontalHeaderLabels(
            {QStringLiteral(""), QStringLiteral("Channel"),
             QStringLiteral("Status"), QStringLiteral("Detail"),
             QStringLiteral("Actions")});
        this->table_->horizontalHeader()->setStretchLastSection(true);
        this->table_->horizontalHeader()->setSectionResizeMode(
            COL_CHANNEL, QHeaderView::ResizeToContents);
        this->table_->horizontalHeader()->setSectionResizeMode(
            COL_STATUS, QHeaderView::ResizeToContents);
        this->table_->verticalHeader()->setVisible(false);
        this->table_->setSelectionMode(QAbstractItemView::NoSelection);
        this->table_->setEditTriggers(QAbstractItemView::NoEditTriggers);
        lay->addWidget(this->table_, 1);

        auto *bulk = new QHBoxLayout;
        auto *banSel = new QPushButton(QStringLiteral("Ban selected"), page);
        auto *toSel =
            new QPushButton(QStringLiteral("Timeout selected..."), page);
        auto *unbanSel = new QPushButton(QStringLiteral("Unban selected"), page);
        bulk->addWidget(banSel);
        bulk->addWidget(toSel);
        bulk->addWidget(unbanSel);
        bulk->addStretch(1);
        lay->addLayout(bulk);

        QObject::connect(banSel, &QPushButton::clicked, this,
                         &LimerinoCrossbanDialog::onBanSelected);
        QObject::connect(toSel, &QPushButton::clicked, this,
                         &LimerinoCrossbanDialog::onTimeoutSelected);
        QObject::connect(unbanSel, &QPushButton::clicked, this,
                         &LimerinoCrossbanDialog::onUnbanSelected);

        this->statusLabel_ = new QLabel(page);
        this->statusLabel_->setWordWrap(true);
        lay->addWidget(this->statusLabel_);

        this->tabs_->addTab(page, QStringLiteral("Channels"));
    }

    // ----- Presets tab -----
    {
        auto *page = new QWidget(this);
        auto *lay = new QHBoxLayout(page);

        auto *left = new QVBoxLayout;
        this->presetList_ = new QListWidget(page);
        left->addWidget(this->presetList_, 1);
        auto *leftBtns = new QHBoxLayout;
        auto *addP = new QPushButton(QStringLiteral("Add"), page);
        this->deletePresetBtn_ = new QPushButton(QStringLiteral("Delete"), page);
        leftBtns->addWidget(addP);
        leftBtns->addWidget(this->deletePresetBtn_);
        left->addLayout(leftBtns);
        lay->addLayout(left, 1);

        QObject::connect(addP, &QPushButton::clicked, this,
                         &LimerinoCrossbanDialog::onAddPreset);
        QObject::connect(this->deletePresetBtn_, &QPushButton::clicked, this,
                         &LimerinoCrossbanDialog::onDeletePreset);
        QObject::connect(this->presetList_, &QListWidget::currentRowChanged,
                         this, [this](int) {
                             this->onPresetSelectionChanged();
                         });

        auto *right = new QVBoxLayout;
        this->presetNameEdit_ = new QLineEdit(page);
        this->presetNameEdit_->setPlaceholderText(QStringLiteral("Preset name"));
        right->addWidget(this->presetNameEdit_);

        this->dynamicHintLabel_ = new QLabel(
            QStringLiteral(
                "This preset always uses every channel you currently "
                "moderate (from Limerino auth). It updates when you gain "
                "or lose mod — no channel list to edit."),
            page);
        this->dynamicHintLabel_->setWordWrap(true);
        this->dynamicHintLabel_->setVisible(false);
        right->addWidget(this->dynamicHintLabel_);

        this->channelInput_ = new QLineEdit(page);
        this->channelInput_->setPlaceholderText(
            QStringLiteral("Add channel login (moderated or not)"));
        right->addWidget(this->channelInput_);

        this->channelList_ = new QListWidget(page);
        right->addWidget(this->channelList_, 1);

        auto *chBtns = new QHBoxLayout;
        this->addChannelBtn_ =
            new QPushButton(QStringLiteral("Add channel"), page);
        this->removeChannelBtn_ =
            new QPushButton(QStringLiteral("Remove selected"), page);
        this->savePresetBtn_ =
            new QPushButton(QStringLiteral("Save preset"), page);
        chBtns->addWidget(this->addChannelBtn_);
        chBtns->addWidget(this->removeChannelBtn_);
        chBtns->addStretch(1);
        chBtns->addWidget(this->savePresetBtn_);
        right->addLayout(chBtns);

        QObject::connect(this->addChannelBtn_, &QPushButton::clicked, this,
                         &LimerinoCrossbanDialog::onAddPresetChannel);
        QObject::connect(this->channelInput_, &QLineEdit::returnPressed, this,
                         &LimerinoCrossbanDialog::onAddPresetChannel);
        QObject::connect(this->removeChannelBtn_, &QPushButton::clicked, this,
                         &LimerinoCrossbanDialog::onRemovePresetChannel);
        QObject::connect(this->savePresetBtn_, &QPushButton::clicked, this,
                         &LimerinoCrossbanDialog::onSavePresetChannels);

        lay->addLayout(right, 2);
        this->tabs_->addTab(page, QStringLiteral("Presets"));
    }

    auto *closeBtn = new QPushButton(QStringLiteral("Close"), this);
    QObject::connect(closeBtn, &QPushButton::clicked, this,
                     &LimerinoCrossbanDialog::close);
    root->addWidget(closeBtn, 0, Qt::AlignRight);
}

void LimerinoCrossbanDialog::rebuildPresetCombo()
{
    const QSignalBlocker block(this->presetCombo_);
    this->presetCombo_->clear();
    if (this->presets_.isEmpty())
    {
        this->presetCombo_->addItem(QStringLiteral("(no presets — edit Presets tab)"),
                                    QVariant());
        return;
    }
    for (const auto &p : this->presets_)
    {
        const int count =
            p.useAllModeratedChannels
                ? collectAllModeratedChannels().size()
                : p.channels.size();
        this->presetCombo_->addItem(
            QStringLiteral("%1 (%2)").arg(p.name).arg(count),
            p.id.toString(QUuid::WithoutBraces));
    }
}

void LimerinoCrossbanDialog::loadSelectedPresetIntoTable()
{
    this->rows_.clear();
    this->table_->setRowCount(0);

    if (this->presets_.isEmpty())
    {
        this->statusLabel_->setText(
            QStringLiteral("No presets yet. Open the Presets tab and add one."));
        return;
    }

    const int idx = this->presetCombo_->currentIndex();
    if (idx < 0 || idx >= this->presets_.size())
    {
        return;
    }
    const auto &preset = this->presets_[idx];
    setLastCrossbanPresetId(preset.id);

    const QVector<CrossbanChannel> channels =
        preset.useAllModeratedChannels ? collectAllModeratedChannels()
                                       : preset.channels;

    if (preset.useAllModeratedChannels && channels.isEmpty())
    {
        this->statusLabel_->setText(QStringLiteral(
            "No moderated channels on your Limerino accounts yet. "
            "Refresh moderated channels from the Limerino settings tab, "
            "or add a custom preset."));
    }

    for (const auto &ch : channels)
    {
        RowState row;
        row.channel = ch;
        row.loading = true;
        this->rows_.append(row);
    }

    this->table_->setRowCount(this->rows_.size());
    for (int i = 0; i < this->rows_.size(); ++i)
    {
        auto *check = new QCheckBox(this->table_);
        check->setChecked(true);
        QObject::connect(check, &QCheckBox::toggled, this, [this, i](bool on) {
            if (i >= 0 && i < this->rows_.size())
            {
                this->rows_[i].checked = on;
            }
        });
        this->table_->setCellWidget(i, COL_CHECK, check);

        const auto &ch = this->rows_[i].channel;
        const QString label =
            ch.displayName.isEmpty()
                ? (ch.login.isEmpty() ? ch.id : ch.login)
                : QStringLiteral("%1 (%2)").arg(ch.displayName, ch.login);
        this->table_->setItem(i, COL_CHANNEL, new QTableWidgetItem(label));
        this->table_->setItem(i, COL_STATUS,
                              new QTableWidgetItem(QStringLiteral("Loading...")));
        this->table_->setItem(i, COL_DETAIL, new QTableWidgetItem(QString()));

        auto *actions = new QWidget(this->table_);
        auto *al = new QHBoxLayout(actions);
        al->setContentsMargins(2, 0, 2, 0);
        auto *ban = new QPushButton(QStringLiteral("Ban"), actions);
        auto *to = new QPushButton(QStringLiteral("TO..."), actions);
        auto *unban = new QPushButton(QStringLiteral("Unban"), actions);
        auto *comments = new QPushButton(QStringLiteral("Notes"), actions);
        al->addWidget(ban);
        al->addWidget(to);
        al->addWidget(unban);
        al->addWidget(comments);
        QObject::connect(ban, &QPushButton::clicked, this,
                         [this, i] { this->onRowBan(i); });
        QObject::connect(to, &QPushButton::clicked, this,
                         [this, i] { this->onRowTimeout(i); });
        QObject::connect(unban, &QPushButton::clicked, this,
                         [this, i] { this->onRowUnban(i); });
        QObject::connect(comments, &QPushButton::clicked, this,
                         [this, i] { this->onRowComments(i); });
        this->table_->setCellWidget(i, COL_ACTIONS, actions);
    }

    this->refreshAllStrikes();
}

void LimerinoCrossbanDialog::refreshAllStrikes()
{
    for (int i = 0; i < this->rows_.size(); ++i)
    {
        this->refreshRowStrike(i);
    }
}

void LimerinoCrossbanDialog::refreshRowStrike(int row)
{
    if (row < 0 || row >= this->rows_.size())
    {
        return;
    }
    auto &rs = this->rows_[row];
    rs.loading = true;
    rs.error.clear();
    this->updateRowUi(row);

    QString err;
    auto token = LimerinoAuth::resolveModerationToken(
        rs.channel.id, rs.channel.login, &err);
    if (!token.hasToken())
    {
        rs.loading = false;
        rs.error = err.isEmpty() ? QStringLiteral("no moderation token") : err;
        this->updateRowUi(row);
        return;
    }

    const QPointer<LimerinoCrossbanDialog> guard(this);
    gql::executePersisted(
        gql::PQ_CHAT_MODERATOR_STRIKE_STATUS,
        QJsonObject{{QStringLiteral("channelID"), rs.channel.id},
                    {QStringLiteral("targetUserID"), this->targetUserId_}},
        token.token,
        [guard, row](const QJsonObject &data) {
            if (!guard || row < 0 || row >= guard->rows_.size())
            {
                return;
            }
            guard->rows_[row].strike = parseStrikeStatus(data);
            guard->rows_[row].loading = false;
            guard->rows_[row].error.clear();
            guard->updateRowUi(row);
        },
        [guard, row](const gql::GqlError &gerr) {
            if (!guard || row < 0 || row >= guard->rows_.size())
            {
                return;
            }
            guard->rows_[row].loading = false;
            guard->rows_[row].error = gerr.message;
            guard->updateRowUi(row);
        });
}

void LimerinoCrossbanDialog::updateRowUi(int row)
{
    if (row < 0 || row >= this->rows_.size() ||
        row >= this->table_->rowCount())
    {
        return;
    }
    const auto &rs = this->rows_[row];
    QString status;
    QString detail;
    if (rs.loading)
    {
        status = QStringLiteral("Loading...");
    }
    else if (!rs.error.isEmpty())
    {
        status = QStringLiteral("Error");
        detail = rs.error;
    }
    else
    {
        status = rs.strike.statusLabel();
        detail = rs.strike.detailLabel();
    }
    this->table_->setItem(row, COL_STATUS, new QTableWidgetItem(status));
    this->table_->setItem(row, COL_DETAIL, new QTableWidgetItem(detail));
}

QVector<int> LimerinoCrossbanDialog::selectedRows() const
{
    QVector<int> out;
    for (int i = 0; i < this->rows_.size(); ++i)
    {
        if (this->rows_[i].checked)
        {
            out.append(i);
        }
    }
    return out;
}

void LimerinoCrossbanDialog::onBanSelected()
{
    this->applyAction(this->selectedRows(), false, std::nullopt);
}

void LimerinoCrossbanDialog::onTimeoutSelected()
{
    bool ok = false;
    const int secs = QInputDialog::getInt(
        this, QStringLiteral("Timeout"),
        QStringLiteral("Duration (seconds) for all selected:"), 600, 1,
        1209600, 1, &ok);
    if (!ok)
    {
        return;
    }
    this->applyAction(this->selectedRows(), true, secs);
}

void LimerinoCrossbanDialog::onUnbanSelected()
{
    this->applyUnban(this->selectedRows());
}

void LimerinoCrossbanDialog::onRowBan(int row)
{
    this->applyAction({row}, false, std::nullopt);
}

void LimerinoCrossbanDialog::onRowTimeout(int row)
{
    bool ok = false;
    const int secs = QInputDialog::getInt(
        this, QStringLiteral("Timeout"),
        QStringLiteral("Duration (seconds):"), 600, 1, 1209600, 1, &ok);
    if (!ok)
    {
        return;
    }
    this->applyAction({row}, true, secs);
}

void LimerinoCrossbanDialog::onRowUnban(int row)
{
    this->applyUnban({row});
}

void LimerinoCrossbanDialog::applyAction(const QVector<int> &rows, bool timeout,
                                         std::optional<int> durationSeconds)
{
    const QString moderatorID = primaryModeratorId();
    if (moderatorID.isEmpty())
    {
        QMessageBox::warning(
            this, QStringLiteral("Crossban"),
            QStringLiteral("Sign in with your primary Twitch account to "
                           "ban / timeout (same as Nuke)."));
        return;
    }
    if (rows.isEmpty())
    {
        return;
    }

    const QString reason = this->reasonEdit_->text();
    const QPointer<LimerinoCrossbanDialog> guard(this);

    for (int row : rows)
    {
        if (row < 0 || row >= this->rows_.size())
        {
            continue;
        }
        const auto &ch = this->rows_[row].channel;
        if (ch.id.isEmpty())
        {
            this->rows_[row].error = QStringLiteral("channel id missing");
            this->updateRowUi(row);
            continue;
        }
        this->rows_[row].loading = true;
        this->updateRowUi(row);

        const QString bucket =
            QStringLiteral("crossban:%1").arg(ch.id);
        LimerinoApi::banUser(
            ch.id, moderatorID, this->targetUserId_,
            timeout ? durationSeconds : std::nullopt, reason, bucket,
            [guard, row] {
                if (!guard || row < 0 || row >= guard->rows_.size())
                {
                    return;
                }
                guard->refreshRowStrike(row);
            },
            [guard, row](const QString &msg) {
                if (!guard || row < 0 || row >= guard->rows_.size())
                {
                    return;
                }
                guard->rows_[row].loading = false;
                guard->rows_[row].error = msg;
                guard->updateRowUi(row);
            });
    }
}

void LimerinoCrossbanDialog::applyUnban(const QVector<int> &rows)
{
    const QString moderatorID = primaryModeratorId();
    if (moderatorID.isEmpty())
    {
        QMessageBox::warning(
            this, QStringLiteral("Crossban"),
            QStringLiteral("Sign in with your primary Twitch account to unban."));
        return;
    }
    const QPointer<LimerinoCrossbanDialog> guard(this);
    for (int row : rows)
    {
        if (row < 0 || row >= this->rows_.size())
        {
            continue;
        }
        const auto &ch = this->rows_[row].channel;
        this->rows_[row].loading = true;
        this->updateRowUi(row);
        LimerinoApi::unbanUser(
            ch.id, moderatorID, this->targetUserId_,
            QStringLiteral("crossban:%1").arg(ch.id),
            [guard, row] {
                if (!guard || row < 0 || row >= guard->rows_.size())
                {
                    return;
                }
                guard->refreshRowStrike(row);
            },
            [guard, row](const QString &msg) {
                if (!guard || row < 0 || row >= guard->rows_.size())
                {
                    return;
                }
                guard->rows_[row].loading = false;
                guard->rows_[row].error = msg;
                guard->updateRowUi(row);
            });
    }
}

void LimerinoCrossbanDialog::onRowComments(int row)
{
    if (row < 0 || row >= this->rows_.size())
    {
        return;
    }
    const auto ch = this->rows_[row].channel;
    QString err;
    auto token =
        LimerinoAuth::resolveModerationToken(ch.id, ch.login, &err);
    if (!token.hasToken())
    {
        QMessageBox::warning(this, QStringLiteral("Notes"),
                             err.isEmpty() ? QStringLiteral("No token") : err);
        return;
    }

    auto *dlg = new BasePopup({BaseWindow::Flags::Dialog}, this);
    dlg->setAttribute(Qt::WA_DeleteOnClose);
    dlg->setWindowTitle(QStringLiteral("Mod notes - #%1").arg(
        ch.login.isEmpty() ? ch.id : ch.login));
    dlg->resize(420, 360);
    auto *lay = new QVBoxLayout(dlg);
    auto *list = new QListWidget(dlg);
    lay->addWidget(list, 1);
    auto *input = new QLineEdit(dlg);
    input->setPlaceholderText(QStringLiteral("New note"));
    lay->addWidget(input);
    auto *addBtn = new QPushButton(QStringLiteral("Add note"), dlg);
    lay->addWidget(addBtn);

    const QString channelId = ch.id;
    const QString targetId = this->targetUserId_;
    const QString gqlToken = token.token;
    const QPointer<QListWidget> listGuard(list);

    auto reload = [channelId, targetId, gqlToken, listGuard] {
        fetchModComments(
            channelId, targetId, gqlToken,
            [listGuard](const QVector<CrossbanComment> &comments) {
                if (!listGuard)
                {
                    return;
                }
                listGuard->clear();
                for (const auto &c : comments)
                {
                    const QString author = c.authorDisplayName.isEmpty()
                                               ? c.authorLogin
                                               : c.authorDisplayName;
                    listGuard->addItem(QStringLiteral("%1 — %2: %3")
                                           .arg(c.timestamp, author, c.text));
                }
            },
            [listGuard](const QString &msg) {
                if (listGuard)
                {
                    listGuard->clear();
                    listGuard->addItem(QStringLiteral("Error: %1").arg(msg));
                }
            });
    };

    QObject::connect(addBtn, &QPushButton::clicked, dlg, [=] {
        const QString text = input->text().trimmed();
        if (text.isEmpty())
        {
            return;
        }
        createModComment(
            channelId, targetId, text, gqlToken,
            [input, reload](const CrossbanComment &) {
                input->clear();
                reload();
            },
            [dlg](const QString &msg) {
                QMessageBox::warning(dlg, QStringLiteral("Notes"), msg);
            });
    });

    reload();
    dlg->show();
}

// ----- Presets tab -----

const CrossbanPreset *LimerinoCrossbanDialog::currentEditingPreset() const
{
    for (const auto &p : this->presets_)
    {
        if (p.id == this->editingPresetId_)
        {
            return &p;
        }
    }
    return nullptr;
}

void LimerinoCrossbanDialog::updatePresetEditorEnabled()
{
    const auto *p = this->currentEditingPreset();
    const bool dynamic = p != nullptr && p->useAllModeratedChannels;
    const bool hasSelection = p != nullptr;

    this->dynamicHintLabel_->setVisible(dynamic);
    this->presetNameEdit_->setEnabled(hasSelection && !dynamic);
    this->channelInput_->setEnabled(hasSelection && !dynamic);
    this->channelList_->setEnabled(hasSelection);
    this->addChannelBtn_->setEnabled(hasSelection && !dynamic);
    this->removeChannelBtn_->setEnabled(hasSelection && !dynamic);
    this->savePresetBtn_->setEnabled(hasSelection && !dynamic);
    this->deletePresetBtn_->setEnabled(hasSelection && !dynamic);
}

void LimerinoCrossbanDialog::rebuildPresetList()
{
    const QSignalBlocker block(this->presetList_);
    this->presetList_->clear();
    for (const auto &p : this->presets_)
    {
        const int count =
            p.useAllModeratedChannels ? collectAllModeratedChannels().size()
                                      : p.channels.size();
        auto *item = new QListWidgetItem(
            QStringLiteral("%1 (%2)").arg(p.name).arg(count),
            this->presetList_);
        item->setData(Qt::UserRole, p.id.toString(QUuid::WithoutBraces));
    }
}

void LimerinoCrossbanDialog::onPresetSelectionChanged()
{
    const auto *item = this->presetList_->currentItem();
    if (item == nullptr)
    {
        this->editingPresetId_ = QUuid();
        this->presetNameEdit_->clear();
        this->channelList_->clear();
        this->updatePresetEditorEnabled();
        return;
    }
    this->editingPresetId_ =
        QUuid::fromString(item->data(Qt::UserRole).toString());
    for (const auto &p : this->presets_)
    {
        if (p.id != this->editingPresetId_)
        {
            continue;
        }
        this->presetNameEdit_->setText(p.name);
        this->channelList_->clear();
        const QVector<CrossbanChannel> channels =
            p.useAllModeratedChannels ? collectAllModeratedChannels()
                                      : p.channels;
        for (const auto &c : channels)
        {
            auto *ci = new QListWidgetItem(
                c.displayName.isEmpty()
                    ? c.login
                    : QStringLiteral("%1 (%2)").arg(c.displayName, c.login),
                this->channelList_);
            ci->setData(Qt::UserRole, c.id);
            ci->setData(Qt::UserRole + 1, c.login);
            ci->setData(Qt::UserRole + 2, c.displayName);
        }
        this->updatePresetEditorEnabled();
        return;
    }
    this->updatePresetEditorEnabled();
}

void LimerinoCrossbanDialog::onAddPreset()
{
    CrossbanPreset p;
    p.id = QUuid::createUuid();
    p.name = QStringLiteral("Preset %1").arg(this->presets_.size() + 1);
    p.useAllModeratedChannels = false;
    this->presets_.append(p);
    saveCrossbanPresets(this->presets_);
    this->rebuildPresetList();
    this->rebuildPresetCombo();
    this->presetList_->setCurrentRow(this->presets_.size() - 1);
}

void LimerinoCrossbanDialog::onDeletePreset()
{
    const int row = this->presetList_->currentRow();
    if (row < 0 || row >= this->presets_.size())
    {
        return;
    }
    if (this->presets_[row].useAllModeratedChannels)
    {
        QMessageBox::information(
            this, QStringLiteral("Cannot delete"),
            QStringLiteral(
                "\"Every moderated channel\" is the built-in default and "
                "cannot be deleted. Add a custom preset for a fixed list."));
        return;
    }
    this->presets_.removeAt(row);
    saveCrossbanPresets(this->presets_);
    // Re-ensure dynamic default still present (no-op if already there).
    if (ensureAllModeratedPreset(this->presets_))
    {
        saveCrossbanPresets(this->presets_);
    }
    this->rebuildPresetList();
    this->rebuildPresetCombo();
    this->loadSelectedPresetIntoTable();
}

void LimerinoCrossbanDialog::onSavePresetChannels()
{
    if (this->editingPresetId_.isNull())
    {
        return;
    }
    for (auto &p : this->presets_)
    {
        if (p.id != this->editingPresetId_)
        {
            continue;
        }
        if (p.useAllModeratedChannels)
        {
            return;
        }
        p.name = this->presetNameEdit_->text().trimmed();
        if (p.name.isEmpty())
        {
            p.name = QStringLiteral("Untitled");
        }
        p.channels.clear();
        for (int i = 0; i < this->channelList_->count(); ++i)
        {
            const auto *item = this->channelList_->item(i);
            p.channels.append(CrossbanChannel{
                item->data(Qt::UserRole).toString(),
                item->data(Qt::UserRole + 1).toString(),
                item->data(Qt::UserRole + 2).toString(),
            });
        }
        break;
    }
    saveCrossbanPresets(this->presets_);
    this->rebuildPresetList();
    this->rebuildPresetCombo();
    this->statusLabel_->setText(QStringLiteral("Preset saved."));
}

void LimerinoCrossbanDialog::onAddPresetChannel()
{
    if (const auto *p = this->currentEditingPreset();
        p != nullptr && p->useAllModeratedChannels)
    {
        return;
    }
    const QString raw = this->channelInput_->text().trimmed();
    if (raw.isEmpty())
    {
        return;
    }

    CrossbanChannel found;
    for (const auto &account : LimerinoAuth::accounts())
    {
        for (const auto &c : account.moderatedChannels)
        {
            if (c.login.compare(raw, Qt::CaseInsensitive) == 0 ||
                c.id == raw)
            {
                found = CrossbanChannel{c.id, c.login, c.displayName};
                break;
            }
        }
        if (!found.id.isEmpty())
        {
            break;
        }
    }
    if (found.id.isEmpty())
    {
        // Allow typing login-only; id filled later when possible.
        found.login = raw.toLower();
        found.displayName = raw;
    }

    auto *item = new QListWidgetItem(
        found.displayName.isEmpty()
            ? found.login
            : QStringLiteral("%1 (%2)").arg(found.displayName, found.login),
        this->channelList_);
    item->setData(Qt::UserRole, found.id);
    item->setData(Qt::UserRole + 1, found.login);
    item->setData(Qt::UserRole + 2, found.displayName);
    this->channelInput_->clear();
}

void LimerinoCrossbanDialog::onRemovePresetChannel()
{
    if (const auto *p = this->currentEditingPreset();
        p != nullptr && p->useAllModeratedChannels)
    {
        return;
    }
    for (auto *item : this->channelList_->selectedItems())
    {
        delete this->channelList_->takeItem(this->channelList_->row(item));
    }
}

void LimerinoCrossbanDialog::fillModeratedIntoInputCompleter()
{
    QStringList logins;
    for (const auto &account : LimerinoAuth::accounts())
    {
        for (const auto &c : account.moderatedChannels)
        {
            if (!c.login.isEmpty() && !logins.contains(c.login, Qt::CaseInsensitive))
            {
                logins.append(c.login);
            }
        }
    }
    logins.sort(Qt::CaseInsensitive);
    auto *completer = new QCompleter(logins, this->channelInput_);
    completer->setCaseSensitivity(Qt::CaseInsensitive);
    this->channelInput_->setCompleter(completer);
}

}  // namespace chatterino::limerino
