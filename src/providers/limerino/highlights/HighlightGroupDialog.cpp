// SPDX-License-Identifier: MIT

#include "providers/limerino/highlights/HighlightGroupDialog.hpp"

#include "common/SignalVector.hpp"
#include "controllers/highlights/HighlightBadge.hpp"
#include "controllers/highlights/HighlightPhrase.hpp"
#include "providers/limerino/highlights/HighlightGroupChannels.hpp"
#include "singletons/Settings.hpp"

#include <QCompleter>
#include <QDialogButtonBox>
#include <QHBoxLayout>
#include <QLabel>
#include <QLineEdit>
#include <QListWidget>
#include <QMessageBox>
#include <QPushButton>
#include <QRadioButton>
#include <QVBoxLayout>

namespace chatterino::limerino {

namespace {

// Qt::UserRole on a QListWidgetItem in the groups list stores the group id
// string (WithoutBraces).
constexpr auto kRoleGroupId = Qt::UserRole + 100;

bool isKnownChannel(const QSet<QString> &known, const QString &entry)
{
    return known.contains(entry);
}

}  // namespace

HighlightGroupDialog::HighlightGroupDialog(QWidget *parent)
    : QDialog(parent)
{
    this->setWindowTitle(QStringLiteral("Highlight groups"));

    auto *outer = new QVBoxLayout(this);

    auto *split = new QHBoxLayout();
    outer->addLayout(split);

    // --- left: groups list + buttons
    auto *left = new QVBoxLayout();
    split->addLayout(left, 1);

    this->groupList_ = new QListWidget(this);
    this->groupList_->setSelectionMode(QAbstractItemView::SingleSelection);
    left->addWidget(this->groupList_);

    auto *leftButtons = new QHBoxLayout();
    left->addLayout(leftButtons);

    auto *addBtn = new QPushButton(QStringLiteral("Add"), this);
    auto *delBtn = new QPushButton(QStringLiteral("Delete"), this);
    leftButtons->addWidget(addBtn);
    leftButtons->addWidget(delBtn);
    leftButtons->addStretch(1);

    QObject::connect(addBtn, &QPushButton::clicked, this,
                     &HighlightGroupDialog::onAddClicked);
    QObject::connect(delBtn, &QPushButton::clicked, this,
                     &HighlightGroupDialog::onDeleteClicked);
    QObject::connect(this->groupList_, &QListWidget::currentItemChanged, this,
                     [this](QListWidgetItem *, QListWidgetItem *) {
                         this->onSelectionChanged();
                     });

    // --- right: editor for the selected group
    auto *right = new QVBoxLayout();
    split->addLayout(right, 2);

    this->nameEdit_ = new QLineEdit(this);
    this->nameEdit_->setPlaceholderText(
        QStringLiteral("Group name (otherwise shown as scope)"));
    right->addWidget(this->nameEdit_);

    this->radioEverywhere_ =
        new QRadioButton(QStringLiteral("Everywhere"), this);
    this->radioAllExcept_ =
        new QRadioButton(QStringLiteral("All except:"), this);
    this->radioOnly_ = new QRadioButton(QStringLiteral("Only:"), this);
    this->radioEverywhere_->setChecked(true);

    right->addWidget(this->radioEverywhere_);
    right->addWidget(this->radioAllExcept_);
    right->addWidget(this->radioOnly_);

    this->channelInput_ = new QLineEdit(this);
    this->channelInput_->setPlaceholderText(
        QStringLiteral("Add channel: forsen, kick:someone, twitch:xqc"));

    const auto known = knownHighlightChannelKeys();
    QStringList knownList(known.begin(), known.end());
    knownList.sort(Qt::CaseInsensitive);
    auto *completer = new QCompleter(knownList, this->channelInput_);
    completer->setCaseSensitivity(Qt::CaseInsensitive);
    this->channelInput_->setCompleter(completer);

    right->addWidget(this->channelInput_);

    this->channelList_ = new QListWidget(this);
    right->addWidget(this->channelList_, 1);

    auto *channelsButtons = new QHBoxLayout();
    right->addLayout(channelsButtons);
    auto *addChannelBtn = new QPushButton(QStringLiteral("Add channel"), this);
    auto *removeChannelBtn =
        new QPushButton(QStringLiteral("Remove selected"), this);
    channelsButtons->addWidget(addChannelBtn);
    channelsButtons->addWidget(removeChannelBtn);
    channelsButtons->addStretch(1);

    QObject::connect(addChannelBtn, &QPushButton::clicked, this, [this] {
        this->addChannelText(this->channelInput_->text());
    });
    QObject::connect(this->channelInput_, &QLineEdit::returnPressed, this,
                     [this] {
                         this->addChannelText(this->channelInput_->text());
                     });
    QObject::connect(removeChannelBtn, &QPushButton::clicked, this, [this] {
        const auto items = this->channelList_->selectedItems();
        for (auto *item : items)
        {
            this->currentChannels_.removeAll(item->data(kRoleGroupId)
                                                 .toString());
            delete this->channelList_->takeItem(
                this->channelList_->row(item));
        }
        this->refreshScopePanel();
    });

    this->memberCountLabel_ = new QLabel(this);
    right->addWidget(this->memberCountLabel_);

    this->deleteHint_ = new QLabel(this);
    this->deleteHint_->setWordWrap(true);
    this->deleteHint_->setStyleSheet(QStringLiteral("color:#a66"));
    right->addWidget(this->deleteHint_);

    // --- bottom dialog buttons
    auto *buttons = new QDialogButtonBox(
        QDialogButtonBox::Ok | QDialogButtonBox::Apply |
            QDialogButtonBox::Cancel,
        this);
    outer->addWidget(buttons);

    QObject::connect(buttons, &QDialogButtonBox::accepted, this, [this] {
        this->applyScopePanel();
        this->accept();
    });
    QObject::connect(buttons->button(QDialogButtonBox::Apply),
                     &QPushButton::clicked, this, [this] {
                         this->applyScopePanel();
                     });
    QObject::connect(buttons, &QDialogButtonBox::rejected, this,
                     [this] { this->reject(); });

    this->rebuildGroupList();

    if (this->groupList_->count() > 0)
    {
        this->groupList_->setCurrentRow(0);
    }

    this->onSelectionChanged();
}

// ---------------------------------------------------------------------------
// helpers

HighlightGroup HighlightGroupDialog::load(const QUuid &id) const
{
    auto items = getSettings()->highlightGroups.readOnly();
    for (const auto &g : *items)
    {
        if (g.id() == id)
        {
            return g;
        }
    }
    return HighlightGroup(HighlightGroup::DEFAULT_ID, {},
                          HighlightGroup::Scope::AllExcept, {});
}

void HighlightGroupDialog::store(const HighlightGroup &group)
{
    auto &vec = getSettings()->highlightGroups;
    auto items = vec.readOnly();
    int idx = 0;
    for (const auto &g : *items)
    {
        if (g.id() == group.id())
        {
            vec.removeAt(idx);
            vec.insert(group, idx);
            return;
        }
        ++idx;
    }
    vec.append(group);
}

int HighlightGroupDialog::countMembers(const QUuid &groupId) const
{
    int count = 0;
    auto messages = getSettings()->highlightedMessages.readOnly();
    for (const auto &p : *messages)
    {
        if (p.groupId() == groupId)
        {
            ++count;
        }
    }
    auto users = getSettings()->highlightedUsers.readOnly();
    for (const auto &u : *users)
    {
        if (u.groupId() == groupId)
        {
            ++count;
        }
    }
    auto badges = getSettings()->highlightedBadges.readOnly();
    for (const auto &b : *badges)
    {
        if (b.groupId() == groupId)
        {
            ++count;
        }
    }
    return count;
}

void HighlightGroupDialog::rebuildGroupList(const QUuid &selectId)
{
    this->suppressSelectionSignals_ = true;
    this->groupList_->clear();

    auto items = getSettings()->highlightGroups.readOnly();
    int rowToSelect = -1;
    int row = 0;
    for (const auto &group : *items)
    {
        auto *item = new QListWidgetItem(group.displayName(),
                                         this->groupList_);
        item->setData(kRoleGroupId,
                      group.id().toString(QUuid::WithoutBraces));
        if (group.isDefault())
        {
            auto font = item->font();
            font.setBold(true);
            item->setFont(font);
            item->setToolTip(QStringLiteral(
                "The Default group cannot be deleted. Any highlight without "
                "a group belongs here."));
        }
        if (!selectId.isNull() && group.id() == selectId)
        {
            rowToSelect = row;
        }
        ++row;
    }

    this->suppressSelectionSignals_ = false;

    if (rowToSelect >= 0)
    {
        this->groupList_->setCurrentRow(rowToSelect);
    }
}

void HighlightGroupDialog::onSelectionChanged()
{
    if (this->suppressSelectionSignals_)
    {
        return;
    }

    auto *item = this->groupList_->currentItem();
    if (!item)
    {
        this->currentId_ = QUuid();
        this->memberCountLabel_->setText({});
        this->deleteHint_->setText({});
        this->channelList_->clear();
        this->currentChannels_.clear();
        return;
    }

    const auto id = QUuid(item->data(kRoleGroupId).toString());
    const auto group = this->load(id);

    this->currentId_ = group.id();
    this->currentScope_ = group.scope();
    this->currentChannels_ = group.channels();

    this->nameEdit_->setText(group.name());

    this->refreshScopePanel();

    const int members = this->countMembers(group.id());
    this->memberCountLabel_->setText(
        QStringLiteral("%1 highlight(s) in this group").arg(members));

    if (group.isDefault())
    {
        this->deleteHint_->setText(
            QStringLiteral("Default group cannot be deleted."));
    }
    else
    {
        this->deleteHint_->clear();
    }
}

void HighlightGroupDialog::refreshScopePanel()
{
    this->suppressSelectionSignals_ = true;
    this->channelList_->clear();

    const auto known = knownHighlightChannelKeys();
    for (const auto &channel : this->currentChannels_)
    {
        auto *item = new QListWidgetItem(channel, this->channelList_);
        item->setData(kRoleGroupId, channel);

        if (!isKnownChannel(known, channel))
        {
            item->setToolTip(QStringLiteral(
                "Unknown channel - this entry does not match any currently "
                "open channel. Check for typos."));
            item->setForeground(QColor(0xaa, 0x44, 0x44));
        }
    }

    this->suppressSelectionSignals_ = false;
}

void HighlightGroupDialog::applyScopePanel()
{
    if (this->currentId_.isNull())
    {
        return;
    }

    auto scope = this->radioOnly_->isChecked()      ? HighlightGroup::Scope::Only
                 : this->radioAllExcept_->isChecked()
                     ? HighlightGroup::Scope::AllExcept
                     : HighlightGroup::Scope::AllExcept;

    this->store(HighlightGroup(this->currentId_,
                               this->nameEdit_->text().trimmed(), scope,
                               this->currentChannels_));
}

void HighlightGroupDialog::addChannelText(const QString &rawText)
{
    const auto text = rawText.trimmed().toLower();
    if (text.isEmpty())
    {
        return;
    }

    // Normalise: users may type "forsen", "#forsen", "twitch:forsen", etc.
    QString normalised;
    if (text.contains(':'))
    {
        normalised = text;
    }
    else if (text.startsWith('#'))
    {
        normalised = QStringLiteral("twitch:") + text.mid(1);
    }
    else
    {
        normalised = QStringLiteral("twitch:") + text;
    }

    if (this->currentChannels_.contains(normalised))
    {
        this->channelInput_->clear();
        return;
    }

    this->currentChannels_.append(normalised);
    this->channelInput_->clear();
    this->refreshScopePanel();
}

void HighlightGroupDialog::onAddClicked()
{
    const auto id = QUuid::createUuid();
    this->store(HighlightGroup(id, {}, HighlightGroup::Scope::AllExcept, {}));
    this->rebuildGroupList(id);
    this->onSelectionChanged();
}

void HighlightGroupDialog::onDeleteClicked()
{
    auto *item = this->groupList_->currentItem();
    if (!item)
    {
        return;
    }

    const auto id = QUuid(item->data(kRoleGroupId).toString());
    if (id.isNull() || id == HighlightGroup::DEFAULT_ID)
    {
        this->deleteHint_->setText(
            QStringLiteral("Default group cannot be deleted."));
        return;
    }

    const int members = this->countMembers(id);
    const auto message = members > 0
                             ? QStringLiteral(
                                   "Delete this group? %1 highlight(s) will "
                                   "be reassigned to the Default group.")
                                   .arg(members)
                             : QStringLiteral(
                                   "Delete this group? It has no highlights.");

    if (QMessageBox::question(this, QStringLiteral("Delete group"), message) !=
        QMessageBox::Yes)
    {
        return;
    }

    // Reassign all members to Default. SignalVector has no in-place
    // mutation and its items are value types, so an item-level rewrite is
    // remove+insert. Helpers rebuild the item with a new groupId.
    auto reassignPhrases = [id](SignalVector<HighlightPhrase> &vec) {
        for (int i = static_cast<int>(vec.raw().size()) - 1; i >= 0; --i)
        {
            const auto &item = vec.raw()[i];
            if (item.groupId() != id)
            {
                continue;
            }
            HighlightPhrase copy(
                item.getPattern(), item.showInMentions(), item.hasAlert(),
                item.hasSound(), item.isRegex(), item.isCaseSensitive(),
                item.getSoundUrl().toString(), item.getColor(),
                HighlightGroup::DEFAULT_ID);
            vec.removeAt(i);
            vec.insert(copy, i);
        }
    };
    auto reassignBadges = [id](SignalVector<HighlightBadge> &vec) {
        for (int i = static_cast<int>(vec.raw().size()) - 1; i >= 0; --i)
        {
            const auto &item = vec.raw()[i];
            if (item.groupId() != id)
            {
                continue;
            }
            HighlightBadge copy(
                item.badgeName(), item.displayName(), item.showInMentions(),
                item.hasAlert(), item.hasSound(),
                item.getSoundUrl().toString(), item.getColor(),
                HighlightGroup::DEFAULT_ID);
            vec.removeAt(i);
            vec.insert(copy, i);
        }
    };

    reassignPhrases(getSettings()->highlightedMessages);
    reassignPhrases(getSettings()->highlightedUsers);
    reassignBadges(getSettings()->highlightedBadges);

    // Remove the group itself.
    auto &groups = getSettings()->highlightGroups;
    auto groupsRo = groups.readOnly();
    for (int i = 0; i < static_cast<int>(groupsRo->size()); ++i)
    {
        if (groupsRo->at(i).id() == id)
        {
            groups.removeAt(i);
            break;
        }
    }

    this->rebuildGroupList();
    this->onSelectionChanged();
}

// ---------------------------------------------------------------------------
// static helper used by the per-row combobox

QUuid HighlightGroupDialog::createGroupModal(QWidget *parent)
{
    HighlightGroupDialog dialog(parent);

    // Pre-create the group so the user sees it in the list immediately.
    const auto id = QUuid::createUuid();
    dialog.store(HighlightGroup(id, {}, HighlightGroup::Scope::AllExcept, {}));
    dialog.rebuildGroupList(id);
    dialog.onSelectionChanged();
    dialog.setWindowTitle(QStringLiteral("New highlight group"));

    if (dialog.exec() != QDialog::Accepted)
    {
        // Roll back the pre-created group so we don't leave an orphan on
        // Cancel.
        auto &groups = getSettings()->highlightGroups;
        auto items = groups.readOnly();
        for (int i = 0; i < static_cast<int>(items->size()); ++i)
        {
            if (items->at(i).id() == id)
            {
                groups.removeAt(i);
                break;
            }
        }
        return QUuid();
    }

    // Apply already ran through the OK-button handler; nothing more to do.
    return id;
}

}  // namespace chatterino::limerino
