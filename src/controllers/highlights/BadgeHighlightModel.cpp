// SPDX-FileCopyrightText: 2021 Contributors to Chatterino <https://chatterino.com>
//
// SPDX-License-Identifier: MIT

#include "controllers/highlights/BadgeHighlightModel.hpp"

#include "Application.hpp"
#include "common/SignalVectorModel.hpp"
#include "controllers/highlights/HighlightBadge.hpp"
#include "controllers/highlights/HighlightPhrase.hpp"
#include "messages/Emote.hpp"
#include "providers/limerino/highlights/HighlightGroup.hpp"
#include "providers/limerino/highlights/HighlightGroupCellDelegate.hpp"
#include "providers/twitch/TwitchBadges.hpp"
#include "singletons/Settings.hpp"
#include "util/StandardItemHelper.hpp"

#include <QUuid>

namespace chatterino {

BadgeHighlightModel::BadgeHighlightModel(QObject *parent)
    : SignalVectorModel<HighlightBadge>(Column::COUNT, parent)
{
    // Limerino: rebuild per-row Group-cell option lists when the group set
    // changes (same hook as HighlightModel).
    this->groupRefreshHolder_.managedConnect(
        getSettings()->highlightGroups.delayedItemsChanged, [this] {
            this->refreshGroupCells();
        });
}

void BadgeHighlightModel::refreshGroupCells()
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
        cell->setData(
            found >= 0 ? names.value(found) : QStringLiteral("Default"),
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

HighlightBadge BadgeHighlightModel::getItemFromRow(
    std::vector<QStandardItem *> &row, const HighlightBadge &original)
{
    using Column = BadgeHighlightModel::Column;

    auto highlightColor = original.getColor();
    *highlightColor =
        row[Column::Color]->data(Qt::DecorationRole).value<QColor>();

    return HighlightBadge{
        original.badgeName(),
        row[Column::Badge]->data(Qt::DisplayRole).toString(),
        row[Column::ShowInMentions]->data(Qt::CheckStateRole).toBool(),
        row[Column::FlashTaskbar]->data(Qt::CheckStateRole).toBool(),
        row[Column::PlaySound]->data(Qt::CheckStateRole).toBool(),
        row[Column::SoundPath]->data(Qt::UserRole).toString(),
        highlightColor,
        QUuid(row[Column::Group]->data(Qt::UserRole).toString()),
    };
}

void BadgeHighlightModel::getRowFromItem(const HighlightBadge &item,
                                         std::vector<QStandardItem *> &row)
{
    using QIconPtr = std::shared_ptr<QIcon>;
    using Column = BadgeHighlightModel::Column;

    setStringItem(row[Column::Badge], item.displayName(), false, true);
    setBoolItem(row[Column::ShowInMentions], item.showInMentions());
    setBoolItem(row[Column::FlashTaskbar], item.hasAlert());
    setBoolItem(row[Column::PlaySound], item.hasSound());
    setFilePathItem(row[Column::SoundPath], item.getSoundUrl());
    setColorItem(row[Column::Color], *item.getColor());

    // Limerino: populate the Group cell same as the other two models so the
    // per-cell combobox delegate can operate here too.
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
    const QString displayName =
        found >= 0 ? names.value(found) : QStringLiteral("Default");

    using Delegate = limerino::HighlightGroupCellDelegate;
    row[Column::Group]->setFlags(
        Qt::ItemFlags(defaultItemFlags(true) | Qt::ItemIsEditable));
    row[Column::Group]->setData(displayName, Qt::DisplayRole);
    row[Column::Group]->setData(groupId.toString(QUuid::WithoutBraces),
                                Qt::UserRole);
    row[Column::Group]->setData(names, Delegate::DisplayNamesRole);
    row[Column::Group]->setData(ids, Delegate::GroupIdsRole);
    row[Column::Group]->setData(false, Delegate::IsPinnedRowRole);

    getApp()->getTwitchBadges()->getBadgeIcon(
        item.badgeName(), [item, row](QString, const QIconPtr pixmap) {
            row[Column::Badge]->setData(QVariant(*pixmap), Qt::DecorationRole);
        });
}

}  // namespace chatterino
