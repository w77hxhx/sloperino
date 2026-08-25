// SPDX-License-Identifier: MIT
// Settings tab for auto-action rules (E5). Owns the list + editor wiring that
// previously lived on LimerinoPage so there is a single editor of the store.

#pragma once

#include "widgets/settingspages/SettingsPage.hpp"

class QLabel;
class QListWidget;
class QPushButton;

namespace chatterino {

class LimerinoAutoActionsPage : public SettingsPage
{
    Q_OBJECT

public:
    LimerinoAutoActionsPage();

    bool filterElements(const QString &query) override;
    void onShow() override;

private:
    void rebuildList();
    void onAdd();
    void onEdit();
    void onDelete();

    QListWidget *list_{};
    QPushButton *addButton_{};
    QPushButton *editButton_{};
    QPushButton *deleteButton_{};
    QLabel *placeholderHelp_{};
};

}  // namespace chatterino
