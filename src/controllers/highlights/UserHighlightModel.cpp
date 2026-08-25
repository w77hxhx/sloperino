// SPDX-FileCopyrightText: 2018 Contributors to Chatterino <https://chatterino.com>
//
// SPDX-License-Identifier: MIT

#include "controllers/highlights/UserHighlightModel.hpp"

#include "Application.hpp"
#include "controllers/highlights/HighlightPhrase.hpp"
#include "providers/colors/ColorProvider.hpp"
#include "providers/limerino/highlights/HighlightGroup.hpp"
#include "providers/limerino/highlights/HighlightGroupCellDelegate.hpp"
#include "singletons/Settings.hpp"
#include "singletons/WindowManager.hpp"
#include "util/StandardItemHelper.hpp"

#include <QUuid>

namespace chatterino {

UserHighlightModel::UserHighlightModel(QObject *parent)
    : SignalVectorModel<HighlightPhrase>(Column::COUNT, parent)
{
    // Limerino: rebuild per-row Group-cell option lists when the group set
    // changes (same hook as HighlightModel).
    this->groupRefreshHolder_.managedConnect(
        getSettings()->highlightGroups.delayedItemsChanged,
        [this] { this->refreshGroupCells(); });
}

void UserHighlightModel::refreshGroupCells()
{
    QStringList names;
    QStringList ids;
    auto groupsVec = getSettings()->highlightGroups.readOnly();
    for (const auto &group : *groupsVec)
    {
        names.append(group.displayName());
        ids.append(group.id().toString(QUuid::WithoutBraces));
    }

    using Delegate = limerino::HighlightGroupCellDelegate;
    for (const auto &modelRow : this->rows())
    {
        if (modelRow.isCustomRow)
        {
            continue;
        }
        auto *cell = modelRow.items[Column::Group];
        cell->setData(names, Delegate::DisplayNamesRole);
        cell->setData(ids, Delegate::GroupIdsRole);

        const int found = ids.indexOf(cell->data(Qt::UserRole).toString());
        cell->setData(found >= 0 ? names.value(found)
                                 : QStringLiteral("Default"),
                      Qt::DisplayRole);
    }

    const int groupRowCount = int(this->rows().size());
    if (groupRowCount > 0)
    {
        const QModelIndex topLeft = this->index(0, Column::Group);
        const QModelIndex bottomRight =
            this->index(groupRowCount - 1, Column::Group);
        QVector<int> roles{Qt::DisplayRole, Delegate::DisplayNamesRole,
                           Delegate::GroupIdsRole};
        this->dataChanged(topLeft, bottomRight, roles);
    }
}

HighlightPhrase UserHighlightModel::getItemFromRow(
    std::vector<QStandardItem *> &row, const HighlightPhrase &original)
{
    auto highlightColor = original.getColor();
    *highlightColor =
        row[Column::Color]->data(Qt::DecorationRole).value<QColor>();

    return HighlightPhrase{
        row[Column::Pattern]->data(Qt::DisplayRole).toString().trimmed(),
        row[Column::ShowInMentions]->data(Qt::CheckStateRole).toBool(),
        row[Column::FlashTaskbar]->data(Qt::CheckStateRole).toBool(),
        row[Column::PlaySound]->data(Qt::CheckStateRole).toBool(),
        row[Column::UseRegex]->data(Qt::CheckStateRole).toBool(),
        row[Column::CaseSensitive]->data(Qt::CheckStateRole).toBool(),
        row[Column::SoundPath]->data(Qt::UserRole).toString(),
        highlightColor,
        QUuid(row[Column::Group]->data(Qt::UserRole).toString()),
    };
}

void UserHighlightModel::afterInit()
{
    std::vector<QStandardItem *> messagesRow = this->createRow();
    setBoolItem(messagesRow[Column::Pattern],
                getSettings()->enableSelfMessageHighlight.getValue(), true,
                false);
    messagesRow[Column::Pattern]->setData("Your messages (automatic)",
                                          Qt::DisplayRole);
    setBoolItem(messagesRow[Column::ShowInMentions],
                getSettings()->showSelfMessageHighlightInMentions.getValue(),
                true, false);
    messagesRow[Column::FlashTaskbar]->setFlags({});
    messagesRow[Column::PlaySound]->setFlags({});
    messagesRow[Column::UseRegex]->setFlags({});
    messagesRow[Column::CaseSensitive]->setFlags({});
    messagesRow[Column::SoundPath]->setFlags({});

    auto selfColor =
        ColorProvider::instance().color(ColorType::SelfMessageHighlight);
    setColorItem(messagesRow[Column::Color], *selfColor, false);

    // Limerino: the pinned SelfMessage row must not expose a Group cell.
    messagesRow[Column::Group]->setFlags(Qt::NoItemFlags);

    this->insertCustomRow(
        messagesRow, HighlightModel::UserHighlightRowIndexes::SelfMessageRow);
}

void UserHighlightModel::customRowSetData(
    const std::vector<QStandardItem *> &row, int column, const QVariant &value,
    int role, int rowIndex)
{
    switch (column)
    {
        case Column::Pattern: {
            if (role == Qt::CheckStateRole)
            {
                if (rowIndex ==
                    HighlightModel::UserHighlightRowIndexes::SelfMessageRow)
                {
                    getSettings()->enableSelfMessageHighlight.setValue(
                        value.toBool());
                }
            }
        }
        break;
        case Column::ShowInMentions: {
            if (role == Qt::CheckStateRole)
            {
                if (rowIndex ==
                    HighlightModel::UserHighlightRowIndexes::SelfMessageRow)
                {
                    getSettings()->showSelfMessageHighlightInMentions.setValue(
                        value.toBool());
                }
            }
        }
        break;
        case Column::Color: {
            if (role == Qt::DecorationRole)
            {
                auto colorName = value.value<QColor>().name(QColor::HexArgb);
                if (rowIndex ==
                    HighlightModel::UserHighlightRowIndexes::SelfMessageRow)
                {
                    getSettings()->selfMessageHighlightColor.setValue(
                        colorName);
                }
            }
        }
        break;
    }

    getApp()->getWindows()->forceLayoutChannelViews();
}

void UserHighlightModel::getRowFromItem(const HighlightPhrase &item,
                                        std::vector<QStandardItem *> &row)
{
    setStringItem(row[Column::Pattern], item.getPattern());
    setBoolItem(row[Column::ShowInMentions], item.showInMentions());
    setBoolItem(row[Column::FlashTaskbar], item.hasAlert());
    setBoolItem(row[Column::PlaySound], item.hasSound());
    setBoolItem(row[Column::UseRegex], item.isRegex());
    setBoolItem(row[Column::CaseSensitive], item.isCaseSensitive());
    setFilePathItem(row[Column::SoundPath], item.getSoundUrl());
    setColorItem(row[Column::Color], *item.getColor());

    // Limerino: populate the Group cell identically to HighlightModel so the
    // per-cell combobox delegate can operate on both tables.
    const QUuid groupId = item.groupId();

    QStringList names;
    QStringList ids;
    auto groupsVec = getSettings()->highlightGroups.readOnly();
    for (const auto &group : *groupsVec)
    {
        names.append(group.displayName());
        ids.append(group.id().toString(QUuid::WithoutBraces));
    }

    int found = ids.indexOf(groupId.toString(QUuid::WithoutBraces));
    const QString displayName = found >= 0 ? names.value(found)
                                           : QStringLiteral("Default");

    using Delegate = limerino::HighlightGroupCellDelegate;
    row[Column::Group]->setFlags(
        Qt::ItemFlags(defaultItemFlags(true) | Qt::ItemIsEditable));
    row[Column::Group]->setData(displayName, Qt::DisplayRole);
    row[Column::Group]->setData(groupId.toString(QUuid::WithoutBraces),
                                Qt::UserRole);
    row[Column::Group]->setData(names, Delegate::DisplayNamesRole);
    row[Column::Group]->setData(ids, Delegate::GroupIdsRole);
    row[Column::Group]->setData(false, Delegate::IsPinnedRowRole);
}

}  // namespace chatterino
