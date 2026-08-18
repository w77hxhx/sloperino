// SPDX-FileCopyrightText: 2026 Contributors to Chatterino <https://chatterino.com>
//
// SPDX-License-Identifier: MIT

#pragma once

#include "common/SignalVectorModel.hpp"
#include "controllers/aliases/EmoteAlias.hpp"

namespace chatterino {

class EmoteAliasesModel : public SignalVectorModel<EmoteAlias>
{
public:
    explicit EmoteAliasesModel(QObject *parent);

protected:
    EmoteAlias getItemFromRow(std::vector<QStandardItem *> &row,
                              const EmoteAlias &original) override;
    void getRowFromItem(const EmoteAlias &item,
                        std::vector<QStandardItem *> &row) override;
};

}  // namespace chatterino
