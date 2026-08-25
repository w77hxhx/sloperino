// SPDX-License-Identifier: MIT
// Crossban dialog: strike status + ban/timeout/unban across a channel preset,
// with a Presets tab for editing the channel list.

#pragma once

#include "providers/limerino/crossban/CrossbanPresets.hpp"
#include "providers/limerino/crossban/CrossbanStrike.hpp"
#include "widgets/BasePopup.hpp"

#include <QString>
#include <QUuid>
#include <QVector>

#include <optional>

class QComboBox;
class QLabel;
class QLineEdit;
class QListWidget;
class QPushButton;
class QTabWidget;
class QTableWidget;
class QCheckBox;

namespace chatterino::limerino {

class LimerinoCrossbanDialog : public BasePopup
{
    Q_OBJECT

public:
    LimerinoCrossbanDialog(QString targetUserId, QString targetLogin,
                           QString targetDisplayName,
                           QWidget *parent = nullptr);

private:
    struct RowState {
        CrossbanChannel channel;
        CrossbanStrike strike;
        QString error;
        bool loading = false;
        bool checked = true;
    };

    void buildUi();
    void rebuildPresetCombo();
    void loadSelectedPresetIntoTable();
    void refreshAllStrikes();
    void refreshRowStrike(int row);
    void updateRowUi(int row);

    void onBanSelected();
    void onTimeoutSelected();
    void onUnbanSelected();
    void onRowBan(int row);
    void onRowTimeout(int row);
    void onRowUnban(int row);
    void onRowComments(int row);

    void applyAction(const QVector<int> &rows, bool timeout,
                     std::optional<int> durationSeconds);
    void applyUnban(const QVector<int> &rows);
    QVector<int> selectedRows() const;

    // Presets tab
    void rebuildPresetList();
    void onPresetSelectionChanged();
    void updatePresetEditorEnabled();
    void onAddPreset();
    void onDeletePreset();
    void onSavePresetChannels();
    void onAddPresetChannel();
    void onRemovePresetChannel();
    void fillModeratedIntoInputCompleter();

    const CrossbanPreset *currentEditingPreset() const;

    QString targetUserId_;
    QString targetLogin_;
    QString targetDisplayName_;

    QVector<CrossbanPreset> presets_;
    QVector<RowState> rows_;

    QTabWidget *tabs_ = nullptr;

    // Channels tab
    QComboBox *presetCombo_ = nullptr;
    QLineEdit *reasonEdit_ = nullptr;
    QTableWidget *table_ = nullptr;
    QLabel *statusLabel_ = nullptr;

    // Presets tab
    QListWidget *presetList_ = nullptr;
    QLineEdit *presetNameEdit_ = nullptr;
    QLabel *dynamicHintLabel_ = nullptr;
    QLineEdit *channelInput_ = nullptr;
    QListWidget *channelList_ = nullptr;
    QPushButton *addChannelBtn_ = nullptr;
    QPushButton *removeChannelBtn_ = nullptr;
    QPushButton *savePresetBtn_ = nullptr;
    QPushButton *deletePresetBtn_ = nullptr;
    QUuid editingPresetId_;
};

}  // namespace chatterino::limerino
