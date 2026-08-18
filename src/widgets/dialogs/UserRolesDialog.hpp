// SPDX-FileCopyrightText: 2026 Contributors to Chatterino <https://chatterino.com>
//
// SPDX-License-Identifier: MIT

#pragma once

#include "providers/roles/RolesApi.hpp"
#include "widgets/DraggablePopup.hpp"

#include <QPointer>
#include <QString>

#include <vector>

class QLabel;
class QLineEdit;
class QPushButton;
class QResizeEvent;
class QScrollArea;
class QScrollBar;
class QShowEvent;
class QVBoxLayout;
class QHBoxLayout;

namespace chatterino {

class Button;
class SvgButton;

class UserRolesDialog : public DraggablePopup
{
public:
    UserRolesDialog(const QString &targetLogin, const QString &displayName = {},
                    const QString &channelName = {}, QWidget *parent = nullptr);

    static void showDialog(const QString &targetLogin,
                           const QString &displayName = {},
                           const QString &channelName = {},
                           QWidget *parent = nullptr);

protected:
    void themeChangedEvent() override;
    void scaleChangedEvent(float scale) override;
    void resizeEvent(QResizeEvent *event) override;
    void showEvent(QShowEvent *event) override;

private:
    void setMode(const QString &mode);
    void setRole(const QString &role);
    void loadSummary();
    void loadRoles(bool force = false);
    void loadNextPage();
    void rebuildContent();
    void clearContent();
    void refreshStyle();
    void updateTabCounters();
    void setStatus(const QString &text, bool error = false);
    void applySizeConstraints();
    void scheduleUnpinParentOnClose(QWidget *parent);

    QString targetLogin_;
    QString displayName_;
    QString channelName_;

    QVBoxLayout *mainLayout_{};
    QWidget *headerWidget_{};
    QLabel *headerTitleLabel_{};
    QLineEdit *searchInput_{};
    Button *pinButton_{};
    SvgButton *closeButton_{};

    // Mode tabs
    QPushButton *channelModeTab_{};
    QPushButton *userModeTab_{};

    // Role filter tabs
    QPushButton *moderatorsTab_{};
    QPushButton *vipsTab_{};
    QPushButton *artistsTab_{};
    QPushButton *foundersTab_{};
    QPushButton *subscribersTab_{};

    QScrollArea *scrollArea_{};
    QWidget *contentWidget_{};
    QVBoxLayout *contentLayout_{};
    QLabel *statusLabel_{};

    QString activeMode_ = QStringLiteral("channel");
    QString activeRole_ = QStringLiteral("moderators");
    RoleSummary summary_;
    bool summaryLoaded_ = false;

    std::vector<RoleItem> items_;
    QString nextCursor_;
    bool hasNextPage_ = false;
    bool itemsLoaded_ = false;
    bool itemsLoading_ = false;
    QString statusText_;
    bool statusIsError_ = false;
    QString searchQuery_;
    bool parentUnpinScheduled_ = false;

    static std::vector<QPointer<UserRolesDialog>> activeDialogs_;
};

}  // namespace chatterino
