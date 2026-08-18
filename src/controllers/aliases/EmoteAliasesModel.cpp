// SPDX-FileCopyrightText: 2026 Contributors to Chatterino <https://chatterino.com>
//
// SPDX-License-Identifier: MIT

#include "controllers/aliases/EmoteAliasesModel.hpp"

#include "controllers/aliases/EmoteAlias.hpp"
#include "util/StandardItemHelper.hpp"

namespace chatterino {

EmoteAliasesModel::EmoteAliasesModel(QObject *parent)
    : SignalVectorModel<EmoteAlias>(3, parent)
{
}

EmoteAlias EmoteAliasesModel::getItemFromRow(std::vector<QStandardItem *> &row,
                                             const EmoteAlias & /*original*/)
{
    return EmoteAlias{row[0]->data(Qt::DisplayRole).toString().trimmed(),
                      row[1]->data(Qt::DisplayRole).toString().trimmed(),
                      row[2]->data(Qt::CheckStateRole).toBool()};
}

void EmoteAliasesModel::getRowFromItem(const EmoteAlias &item,
                                       std::vector<QStandardItem *> &row)
{
    setStringItem(row[0], item.word());
    setStringItem(row[1], item.link());
    setBoolItem(row[2], item.isCaseSensitive());
}

}  // namespace chatterino
