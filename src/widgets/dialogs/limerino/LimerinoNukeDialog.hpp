// SPDX-License-Identifier: MIT
// Nuke dialog (batch N3).
//
// One dialog per channel. Preview first; Execute disabled until a preview
// produced targets. Execution routes through NukeExecutor — a serial queue on
// top of LimerinoRateLimiter — with a Cancel button and progress feedback.
// Deletes cannot be undone, and that is stated next to the action selection.

#pragma once

#include "widgets/BasePopup.hpp"

#include "providers/limerino/matcher/LimerinoMatcher.hpp"
#include "providers/limerino/nuke/NukePlan.hpp"

#include <QPointer>
#include <QString>
#include <QStringList>

class QCheckBox;
class QComboBox;
class QLabel;
class QLineEdit;
class QPlainTextEdit;
class QProgressBar;
class QPushButton;
class QSpinBox;
class QListWidget;

#include <vector>

namespace chatterino {

class Channel;
class Split;
using ChannelPtr = std::shared_ptr<Channel>;

namespace limerino {

struct LimerinoNukePreset;
class NukeExecutor;

class LimerinoNukeDialog : public BasePopup
{
    Q_OBJECT

public:
    /// `channel` is kept as a ChannelPtr so the dialog survives the split
    /// closing mid-execution; the underlying Channel never dies while a shared
    /// pointer to it lives.
    explicit LimerinoNukeDialog(Split *split, ChannelPtr channel);

private:
    void onFieldChanged();
    void onPreview();
    void onExecute();
    void onCancel();
    void onProgress(int done, int total, int succeeded, int failed);
    void onFinished(bool cancelled, const QStringList &failures);

    // Presets (batch N4)
    void refreshPresetList();
    void onPresetSave();
    void onPresetDelete();
    void onPresetSelected(int row);
    void applyPreset(const LimerinoNukePreset &preset);

    // Undo of the most recent completed run in this channel (batch N4)
    void onUndoLast();

    LimerinoMatcher contentMatcher() const;
    LimerinoMatcher senderMatcher() const;
    NukeAction currentAction() const;
    bool validateInputs();

    ChannelPtr channel_;

    // Inputs
    QLineEdit *contentEdit_ = nullptr;
    QCheckBox *contentRegex_ = nullptr;
    QCheckBox *contentCase_ = nullptr;
    QLabel *contentError_ = nullptr;

    QLineEdit *senderEdit_ = nullptr;
    QCheckBox *senderRegex_ = nullptr;
    QCheckBox *senderCase_ = nullptr;
    QLabel *senderError_ = nullptr;

    QSpinBox *lookbackSpin_ = nullptr;
    QComboBox *actionCombo_ = nullptr;
    QSpinBox *timeoutSpin_ = nullptr;  // shown for Timeout & DeleteAndTimeout
    QLineEdit *reasonEdit_ = nullptr;  // shown for Ban/Warn/Timeout
    QLabel *irreversibleLabel_ = nullptr;

    // Preview / confirmation
    QPushButton *previewButton_ = nullptr;
    QLabel *previewSummary_ = nullptr;
    QLabel *previewCoverage_ = nullptr;
    QPlainTextEdit *previewTargets_ = nullptr;

    // Execution
    QPushButton *executeButton_ = nullptr;
    QPushButton *cancelButton_ = nullptr;
    QPushButton *undoButton_ = nullptr;  // visible after a completed run
    QProgressBar *progressBar_ = nullptr;
    QLabel *progressLabel_ = nullptr;
    QPlainTextEdit *resultsView_ = nullptr;

    // Presets
    QListWidget *presetList_ = nullptr;
    QPushButton *presetSaveButton_ = nullptr;
    QPushButton *presetDeleteButton_ = nullptr;

    QPointer<NukeExecutor> executor_;
};

}  // namespace limerino
}  // namespace chatterino
