// SPDX-License-Identifier: MIT
// Dialog for the secondary "Limerino Extra Features" auth.
// Deliberately a NEW dialog; the stock LoginDialog is untouched.

#pragma once

#include "providers/limerino/LimerinoAuth.hpp"
#include "widgets/BasePopup.hpp"

#include <QPointer>

#include <pajlada/signals/signalholder.hpp>

class QComboBox;
class QLabel;
class QPushButton;
class QTabWidget;
class QTableWidget;

namespace chatterino {

class LimerinoAuthDialog final : public BasePopup
{
    Q_OBJECT

public:
    explicit LimerinoAuthDialog(QWidget *parent = nullptr);

private:
    void rebuildAccountsTable();
    void setDeviceStatusText(
        const LimerinoAuth::DeviceLogin::Status &status);
    void updateDeviceResult();

    struct {
        QTabWidget *tabs = nullptr;

        QLabel *deviceStatus = nullptr;
        QLabel *deviceCode = nullptr;
        QLabel *deviceLink = nullptr;
        QPushButton *deviceLinkCopy = nullptr;
        QLabel *deviceResult = nullptr;
        QPushButton *deviceStart = nullptr;
        QPushButton *deviceCancel = nullptr;

        QTableWidget *accountsTable = nullptr;
        QLabel *accountsSummary = nullptr;
    } ui_;

    QPointer<LimerinoAuth::DeviceLogin> deviceLogin_;
    pajlada::Signals::SignalHolder managedConnections_;

    // Success-state tracking: accounts known before the current attempt and
    // the verification URI of the in-flight attempt (for the copy button).
    QStringList knownUserIds_;
    QString newAccountUserId_;
    QString currentVerificationUri_;
    QDateTime generatedAt_;
};

}  // namespace chatterino
