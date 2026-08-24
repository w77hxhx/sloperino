// SPDX-FileCopyrightText: 2026 Contributors to Chatterino <https://chatterino.com>
//
// SPDX-License-Identifier: MIT

#pragma once

#include "widgets/settingspages/SettingsPage.hpp"

#include <QLabel>
#include <QTimer>
#include <QVector>

namespace chatterino {

class GeneralPageView;

class SloperinoPage : public SettingsPage
{
    Q_OBJECT

public:
    SloperinoPage();

    bool filterElements(const QString &query) override;

private:
    void initLayout(GeneralPageView &layout);
    void refreshEndpointStatuses();
    void updateSeventvStatus();
    void openSeventvAuthDialog();
    void refreshSeventvAccount();
    void removeSeventvAccount();

    GeneralPageView *view_{};

    // 7TV Authentication widgets
    QLabel *seventvStatusLabel_{nullptr};
    QLabel *seventvDetailsLabel_{nullptr};
    QPushButton *addSeventvBtn_{nullptr};
    QPushButton *refreshSeventvBtn_{nullptr};
    QPushButton *removeSeventvBtn_{nullptr};

    // Live endpoint status labels (one per firehose endpoint)
    QVector<QLabel *> endpointStatusLabels_;
    QTimer *statusRefreshTimer_{nullptr};
};

}  // namespace chatterino
