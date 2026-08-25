// SPDX-License-Identifier: MIT

#include "providers/limerino/highlights/HighlightGroupCellDelegate.hpp"

#include "providers/limerino/highlights/HighlightGroupDialog.hpp"
#include "singletons/Settings.hpp"

#include <QComboBox>
#include <QStringList>

namespace chatterino::limerino {

HighlightGroupCellDelegate::HighlightGroupCellDelegate(QObject *parent)
    : QStyledItemDelegate(parent)
{
}

std::vector<HighlightGroupCellDelegate::Entry>
    HighlightGroupCellDelegate::entries(const QModelIndex &index) const
{
    std::vector<Entry> out;

    const QStringList names =
        index.data(DisplayNamesRole).toStringList();
    const QStringList ids =
        index.data(GroupIdsRole).toStringList();

    // First entry: Default group (explicitly the fallback).
    out.push_back({names.isEmpty() ? QStringLiteral("Default")
                                   : names.value(0),
                   QUuid(ids.isEmpty()
                             ? QString()
                             : ids.value(0))});

    for (int i = 1; i < names.size(); ++i)
    {
        out.push_back({names.value(i), QUuid(ids.value(i))});
    }

    // Trailing "New group…" uses a null UUID.
    out.push_back({QStringLiteral("New group…"), QUuid()});

    return out;
}

QWidget *HighlightGroupCellDelegate::createEditor(
    QWidget *parent, const QStyleOptionViewItem &option,
    const QModelIndex &index) const
{
    // Pinned (built-in) rows are never editable.
    if (index.data(IsPinnedRowRole).toBool())
    {
        return nullptr;
    }

    auto items = this->entries(index);
    if (items.size() <= 1)
    {
        // Nothing but "New group…" -- still allow it.
    }

    auto *combo = new QComboBox(parent);
    for (const auto &entry : items)
    {
        combo->addItem(entry.text, entry.groupId.toString());
    }
    return combo;
}

void HighlightGroupCellDelegate::setEditorData(QWidget *editor,
                                               const QModelIndex &index) const
{
    auto *combo = qobject_cast<QComboBox *>(editor);
    if (!combo)
    {
        QStyledItemDelegate::setEditorData(editor, index);
        return;
    }
    const QString currentId = index.data(Qt::UserRole).toString();
    const int found = combo->findData(currentId);
    combo->setCurrentIndex(found >= 0 ? found : 0);
}

void HighlightGroupCellDelegate::setModelData(
    QWidget *editor, QAbstractItemModel *model, const QModelIndex &index) const
{
    auto *combo = qobject_cast<QComboBox *>(editor);
    if (!combo)
    {
        QStyledItemDelegate::setModelData(editor, model, index);
        return;
    }

    const QString chosenId = combo->currentData().toString();

    if (chosenId.isEmpty())
    {
        // User picked "New group…" -- open the manager dialog, pre-fill a
        // fresh group, assign it here on accept.
        const auto newId = HighlightGroupDialog::createGroupModal(
            qobject_cast<QWidget *>(this->parent()));
        if (!newId.isNull())
        {
            model->setData(index, newId.toString(QUuid::WithoutBraces),
                           Qt::UserRole);
            // Look up the freshly-created group's display name so the cell
            // text updates immediately. The model's remove+insert cycle only
            // persists the id; it does not re-derive the display string.
            auto groupsVec = getSettings()->highlightGroups.readOnly();
            for (const auto &g : *groupsVec)
            {
                if (g.id() == newId)
                {
                    model->setData(index, g.displayName(), Qt::DisplayRole);
                    break;
                }
            }
        }
        return;
    }

    model->setData(index, combo->currentText(), Qt::DisplayRole);
    model->setData(index, chosenId, Qt::UserRole);
}

}  // namespace chatterino::limerino
