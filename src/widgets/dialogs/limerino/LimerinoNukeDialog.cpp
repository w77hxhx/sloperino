// SPDX-License-Identifier: MIT

#include "widgets/dialogs/limerino/LimerinoNukeDialog.hpp"

#include "common/Channel.hpp"
#include "providers/kick/KickChannel.hpp"
#include "providers/limerino/matcher/LimerinoMatcher.hpp"
#include "providers/limerino/matcher/LimerinoMatcherValidation.hpp"
#include "providers/limerino/nuke/NukeEngine.hpp"
#include "providers/limerino/nuke/NukeExecutor.hpp"
#include "providers/limerino/nuke/NukePreset.hpp"
#include "providers/limerino/nuke/NukePresetsStore.hpp"
#include "providers/twitch/TwitchChannel.hpp"
#include "util/LayoutHelper.hpp"
#include "widgets/splits/Split.hpp"

#include <QCheckBox>
#include <QComboBox>
#include <QCursor>
#include <QDateTime>
#include <QFormLayout>
#include <QFrame>
#include <QGuiApplication>
#include <QInputDialog>
#include <QLabel>
#include <QLineEdit>
#include <QListWidget>
#include <QMessageBox>
#include <QPlainTextEdit>
#include <QProgressBar>
#include <QPushButton>
#include <QScreen>
#include <QScrollArea>
#include <QSpinBox>
#include <QVBoxLayout>

namespace chatterino::limerino {

namespace {

constexpr int DEFAULT_LOOKBACK_SEC = 600;
constexpr int DEFAULT_TIMEOUT_SEC = 600;

QString actionLabel(NukeAction action)
{
    switch (action)
    {
        case NukeAction::Ban:
            return QStringLiteral("Ban");
        case NukeAction::Timeout:
            return QStringLiteral("Timeout");
        case NukeAction::Warn:
            return QStringLiteral("Warn");
        case NukeAction::Delete:
            return QStringLiteral("Delete messages");
        case NukeAction::DeleteAndTimeout:
            return QStringLiteral("Delete messages and timeout senders");
    }
    return {};
}

// Cap dialog size to the screen it opens on (DPI/multi-monitor safe).
void fitDialogToAvailableScreen(QWidget *dialog, int preferredWidth,
                                int preferredHeight)
{
    QScreen *screen = dialog->screen();
    if (screen == nullptr && dialog->parentWidget() != nullptr)
    {
        screen = dialog->parentWidget()->screen();
    }
    if (screen == nullptr)
    {
        screen = QGuiApplication::screenAt(QCursor::pos());
    }
    if (screen == nullptr)
    {
        screen = QGuiApplication::primaryScreen();
    }
    if (screen == nullptr)
    {
        dialog->resize(preferredWidth, preferredHeight);
        return;
    }
    const QRect avail = screen->availableGeometry();
    constexpr int margin = 48;
    const int maxW = qMax(320, avail.width() - margin);
    const int maxH = qMax(240, avail.height() - margin);
    dialog->setMaximumSize(maxW, maxH);
    dialog->resize(qMin(preferredWidth, maxW), qMin(preferredHeight, maxH));
}

void boundPlainTextHeight(QPlainTextEdit *edit, int visibleLines)
{
    const int lineH = edit->fontMetrics().lineSpacing();
    const int pad = edit->contentsMargins().top() +
                    edit->contentsMargins().bottom() + 8;
    edit->setMinimumHeight(lineH * 3 + pad);
    edit->setMaximumHeight(lineH * visibleLines + pad);
}

}  // namespace

LimerinoNukeDialog::LimerinoNukeDialog(Split *split, ChannelPtr channel)
    : BasePopup({BaseWindow::Flags::Dialog}, split)
    , channel_(std::move(channel))
{
    const bool isKick = this->channel_->isKickChannel();
    const QString channelName = this->channel_->getName();

    this->setWindowTitle(
        QStringLiteral("Nuke messages - #%1 (Limerino)").arg(channelName));

    auto *outer = new QVBoxLayout(this);

    auto *scroll = new QScrollArea(this);
    scroll->setWidgetResizable(true);
    scroll->setFrameShape(QFrame::NoFrame);
    scroll->setHorizontalScrollBarPolicy(Qt::ScrollBarAsNeeded);
    scroll->setVerticalScrollBarPolicy(Qt::ScrollBarAsNeeded);

    auto *content = new QWidget;
    auto *layout = new QVBoxLayout(content);
    auto *form = new QFormLayout();
    layout->addLayout(form);

    // --- content matcher ---
    {
        this->contentEdit_ = new QLineEdit(this);
        this->contentRegex_ = new QCheckBox(QStringLiteral("Regex"), this);
        this->contentCase_ =
            new QCheckBox(QStringLiteral("Case sensitive"), this);
        this->contentError_ = new QLabel(this);
        this->contentError_->setWordWrap(true);
        this->contentError_->setStyleSheet(
            QStringLiteral("color: #d9534f;"));

        auto *row = new QHBoxLayout();
        row->setContentsMargins(0, 0, 0, 0);
        row->addWidget(this->contentEdit_, 1);
        row->addWidget(this->contentRegex_);
        row->addWidget(this->contentCase_);

        form->addRow(QStringLiteral("Message matcher"), row);
        form->addRow(QString(), this->contentError_);
    }

    // --- sender matcher ---
    {
        this->senderEdit_ = new QLineEdit(this);
        this->senderRegex_ = new QCheckBox(QStringLiteral("Regex"), this);
        this->senderCase_ =
            new QCheckBox(QStringLiteral("Case sensitive"), this);
        this->senderError_ = new QLabel(this);
        this->senderError_->setWordWrap(true);
        this->senderError_->setStyleSheet(QStringLiteral("color: #d9534f;"));

        auto *row = new QHBoxLayout();
        row->setContentsMargins(0, 0, 0, 0);
        row->addWidget(this->senderEdit_, 1);
        row->addWidget(this->senderRegex_);
        row->addWidget(this->senderCase_);

        form->addRow(QStringLiteral("Sender matcher"), row);
        form->addRow(QString(), this->senderError_);
    }

    // --- lookback ---
    {
        this->lookbackSpin_ = new QSpinBox(this);
        this->lookbackSpin_->setRange(1, 86400);
        this->lookbackSpin_->setSuffix(QStringLiteral(" s"));
        this->lookbackSpin_->setValue(DEFAULT_LOOKBACK_SEC);
        form->addRow(QStringLiteral("Lookback"), this->lookbackSpin_);
    }

    // --- action ---
    {
        this->actionCombo_ = new QComboBox(this);
        this->actionCombo_->addItem(actionLabel(NukeAction::Ban),
                                    static_cast<int>(NukeAction::Ban));
        this->actionCombo_->addItem(actionLabel(NukeAction::Timeout),
                                    static_cast<int>(NukeAction::Timeout));
        this->actionCombo_->addItem(actionLabel(NukeAction::Delete),
                                    static_cast<int>(NukeAction::Delete));
        this->actionCombo_->addItem(
            actionLabel(NukeAction::DeleteAndTimeout),
            static_cast<int>(NukeAction::DeleteAndTimeout));
        this->actionCombo_->addItem(actionLabel(NukeAction::Warn),
                                    static_cast<int>(NukeAction::Warn));
        if (isKick)
        {
            const int idx = this->actionCombo_->findData(
                static_cast<int>(NukeAction::Warn));
            if (idx >= 0)
            {
                this->actionCombo_->removeItem(idx);
            }
        }
        form->addRow(QStringLiteral("Action"), this->actionCombo_);
    }

    // --- timeout duration ---
    {
        this->timeoutSpin_ = new QSpinBox(this);
        this->timeoutSpin_->setRange(1, 1209600);  // 2 weeks max
        this->timeoutSpin_->setSuffix(QStringLiteral(" s"));
        this->timeoutSpin_->setValue(DEFAULT_TIMEOUT_SEC);
        form->addRow(QStringLiteral("Timeout duration"), this->timeoutSpin_);
    }

    // --- reason ---
    {
        this->reasonEdit_ = new QLineEdit(this);
        form->addRow(QStringLiteral("Reason"), this->reasonEdit_);
    }

    // --- delete irreversibility note ---
    {
        this->irreversibleLabel_ = new QLabel(
            QStringLiteral("Delete actions are irreversible and cannot be "
                           "undone. Timeouts clear a user's recent messages."),
            this);
        this->irreversibleLabel_->setWordWrap(true);
        this->irreversibleLabel_->setStyleSheet(
            QStringLiteral("color: #f0ad4e;"));
        form->addRow(QString(), this->irreversibleLabel_);
    }

    // --- preview row ---
    {
        auto *row = new QHBoxLayout();
        row->setContentsMargins(0, 0, 0, 0);
        this->previewButton_ =
            new QPushButton(QStringLiteral("Preview"), this);
        this->previewSummary_ = new QLabel(QStringLiteral("Not previewed."),
                                           this);
        this->previewSummary_->setWordWrap(true);
        row->addWidget(this->previewButton_);
        row->addWidget(this->previewSummary_, 1);
        form->addRow(QString(), row);
    }

    // --- buffer coverage display ---
    {
        this->previewCoverage_ = new QLabel(this);
        this->previewCoverage_->setWordWrap(true);
        this->previewCoverage_->setStyleSheet(
            QStringLiteral("color: #f0ad4e;"));
        form->addRow(QString(), this->previewCoverage_);
    }

    // --- preview targets list ---
    {
        this->previewTargets_ = new QPlainTextEdit(this);
        this->previewTargets_->setReadOnly(true);
        boundPlainTextHeight(this->previewTargets_, 12);
        form->addRow(QStringLiteral("Targets"), this->previewTargets_);
    }

    // --- progress ---
    {
        this->progressBar_ = new QProgressBar(this);
        this->progressBar_->setVisible(false);
        this->progressLabel_ = new QLabel(this);
        form->addRow(QString(), this->progressBar_);
        form->addRow(QString(), this->progressLabel_);
    }

    // --- results log ---
    {
        this->resultsView_ = new QPlainTextEdit(this);
        this->resultsView_->setReadOnly(true);
        boundPlainTextHeight(this->resultsView_, 8);
        form->addRow(QStringLiteral("Results"), this->resultsView_);
    }

    // --- presets (batch N4) ---
    {
        auto *presetBox = new QHBoxLayout();
        presetBox->setContentsMargins(0, 0, 0, 0);
        this->presetList_ = new QListWidget(this);
        this->presetList_->setMaximumHeight(90);
        this->presetSaveButton_ =
            new QPushButton(QStringLiteral("Save preset"), this);
        this->presetDeleteButton_ =
            new QPushButton(QStringLiteral("Delete preset"), this);
        auto *presetButtons = new QVBoxLayout();
        presetButtons->addWidget(this->presetSaveButton_);
        presetButtons->addWidget(this->presetDeleteButton_);
        presetBox->addWidget(this->presetList_, 1);
        presetBox->addLayout(presetButtons);
        form->addRow(QStringLiteral("Presets"), presetBox);
    }

    scroll->setWidget(content);
    outer->addWidget(scroll, 1);

    // Execute / Cancel / Undo stay fixed outside the scroll area.
    {
        auto *row = new QHBoxLayout();
        row->setContentsMargins(0, 0, 0, 0);
        this->executeButton_ =
            new QPushButton(QStringLiteral("Execute"), this);
        this->cancelButton_ =
            new QPushButton(QStringLiteral("Cancel"), this);
        this->cancelButton_->setVisible(false);
        this->undoButton_ = new QPushButton(QStringLiteral("Undo last"), this);
        this->undoButton_->setToolTip(QStringLiteral(
            "Reverse the bans/timeouts from the most recent completed nuke "
            "in this channel. Deletes cannot be undone."));
        row->addWidget(this->executeButton_);
        row->addWidget(this->cancelButton_);
        row->addWidget(this->undoButton_);
        row->addStretch(1);
        outer->addLayout(row);
    }

    fitDialogToAvailableScreen(this, 520, 560);

    // --- wiring -------------------------------------------------------
    auto invalidate = [this] {
        this->onFieldChanged();
    };
    QObject::connect(this->contentEdit_, &QLineEdit::textChanged, this,
                     invalidate);
    QObject::connect(this->senderEdit_, &QLineEdit::textChanged, this,
                     invalidate);
    QObject::connect(this->contentRegex_, &QCheckBox::toggled, this,
                     invalidate);
    QObject::connect(this->contentCase_, &QCheckBox::toggled, this, invalidate);
    QObject::connect(this->senderRegex_, &QCheckBox::toggled, this, invalidate);
    QObject::connect(this->senderCase_, &QCheckBox::toggled, this, invalidate);
#if QT_VERSION >= QT_VERSION_CHECK(6, 0, 0)
    QObject::connect(this->lookbackSpin_,
                     qOverload<int>(&QSpinBox::valueChanged), this, invalidate);
#else
    QObject::connect(this->lookbackSpin_,
                     QOverload<int>::of(&QSpinBox::valueChanged), this,
                     invalidate);
#endif
    QObject::connect(this->actionCombo_,
                     QOverload<int>::of(&QComboBox::currentIndexChanged), this,
                     [this](int) {
                         this->onFieldChanged();
                         const auto a = this->currentAction();
                         const bool isTime = a == NukeAction::Timeout ||
                                             a == NukeAction::DeleteAndTimeout;
                         const bool usesReason =
                             a == NukeAction::Ban || a == NukeAction::Timeout ||
                             a == NukeAction::Warn;
                         const bool usesDelete =
                             a == NukeAction::Delete ||
                             a == NukeAction::DeleteAndTimeout;
                         this->timeoutSpin_->setVisible(isTime);
                         this->reasonEdit_->setVisible(usesReason);
                         this->irreversibleLabel_->setVisible(usesDelete);
                     });

    QObject::connect(this->previewButton_, &QPushButton::clicked, this,
                     [this] {
                         this->onPreview();
                     });
    QObject::connect(this->executeButton_, &QPushButton::clicked, this,
                     [this] {
                         this->onExecute();
                     });
    QObject::connect(this->cancelButton_, &QPushButton::clicked, this,
                     [this] {
                         this->onCancel();
                     });

    QObject::connect(this->undoButton_, &QPushButton::clicked, this,
                     [this] {
                         this->onUndoLast();
                     });

    QObject::connect(this->presetList_, &QListWidget::currentRowChanged, this,
                     [this](int row) {
                         this->onPresetSelected(row);
                     });
    QObject::connect(this->presetSaveButton_, &QPushButton::clicked, this,
                     [this] {
                         this->onPresetSave();
                     });
    QObject::connect(this->presetDeleteButton_, &QPushButton::clicked, this,
                     [this] {
                         this->onPresetDelete();
                     });

    // Initial state: no preview yet, so Execute stays disabled.
    this->executeButton_->setEnabled(false);
    // Default action: Ban -> reason visible, timeout hidden.
    this->timeoutSpin_->setVisible(false);
    this->irreversibleLabel_->setVisible(false);

    this->refreshPresetList();
    this->onFieldChanged();  // populate initial validation messages
}

LimerinoMatcher LimerinoNukeDialog::contentMatcher() const
{
    return {this->contentEdit_->text(), this->contentCase_->isChecked(),
            this->contentRegex_->isChecked()};
}

LimerinoMatcher LimerinoNukeDialog::senderMatcher() const
{
    return {this->senderEdit_->text(), this->senderCase_->isChecked(),
            this->senderRegex_->isChecked()};
}

NukeAction LimerinoNukeDialog::currentAction() const
{
    return static_cast<NukeAction>(this->actionCombo_->currentData().toInt());
}

bool LimerinoNukeDialog::validateInputs()
{
    bool ok = true;

    auto checkMatcher = [](const LimerinoMatcher &m, QLabel *label) {
        if (auto err = m.compileError())
        {
            label->setText(QStringLiteral("Invalid regex: ") + *err);
            return false;
        }
        label->clear();
        return true;
    };

    if (!checkMatcher(this->contentMatcher(), this->contentError_))
    {
        ok = false;
    }
    if (!checkMatcher(this->senderMatcher(), this->senderError_))
    {
        ok = false;
    }

    if (ok)
    {
        if (auto err = validateNukeRequest(this->contentMatcher(),
                                           this->senderMatcher(),
                                           this->lookbackSpin_->value()))
        {
            this->previewSummary_->setText(*err);
            ok = false;
        }
    }

    if (ok && this->currentAction() == NukeAction::Warn &&
        this->reasonEdit_->text().trimmed().isEmpty())
    {
        this->previewSummary_->setText(
            QStringLiteral("Warn requires a reason."));
        ok = false;
    }

    this->previewButton_->setEnabled(ok && !this->executor_);
    return ok;
}

void LimerinoNukeDialog::onFieldChanged()
{
    // Any change invalidates a previous preview: Execute only enables after a
    // fresh preview of the field values that are about to run.
    this->executeButton_->setEnabled(false);
    if (this->validateInputs())
    {
        this->previewSummary_->setText(
            QStringLiteral("Press Preview to see what would be affected."));
    }
}

void LimerinoNukeDialog::onPreview()
{
    if (!this->channel_)
    {
        this->previewSummary_->setText(QStringLiteral("Channel is gone."));
        this->executeButton_->setEnabled(false);
        return;
    }
    if (!this->validateInputs())
    {
        this->executeButton_->setEnabled(false);
        return;
    }

    const auto plan = buildPlan(this->channel_->getMessageSnapshot(),
                                this->channel_->messagePlatform(),
                                this->channel_->getName(),
                                nukeSelfLogin(*this->channel_),
                                this->contentMatcher(), this->senderMatcher(),
                                this->lookbackSpin_->value(),
                                this->currentAction());

    // Summarise.
    QString summary;
    if (this->currentAction() == NukeAction::Delete)
    {
        summary = QStringLiteral("%1 message(s) matched.")
                      .arg(plan.messagesMatched);
    }
    else
    {
        summary = QStringLiteral("%1 user(s) affected across %2 matched "
                                 "message(s).")
                      .arg(plan.targets.size())
                      .arg(plan.messagesMatched);
    }
    if (!plan.warnings.isEmpty())
    {
        summary += QStringLiteral("  ") +
                   plan.warnings.join(QStringLiteral(" · "));
    }
    this->previewSummary_->setText(summary);

    // Buffer coverage readout + prominent warning when the lookback overruns.
    QString coverage;
    if (plan.bufferOldest.isValid())
    {
        coverage = QStringLiteral("Buffer covers %1 → %2.")
                       .arg(plan.bufferOldest.toLocalTime().toString(
                           QStringLiteral("HH:mm:ss")))
                       .arg(plan.bufferNewest.toLocalTime().toString(
                           QStringLiteral("HH:mm:ss")));
        const auto coveredSecs =
            plan.bufferNewest.toSecsSinceEpoch() -
            plan.bufferOldest.toSecsSinceEpoch();
        coverage += QStringLiteral(" (about %1 s of history)")
                        .arg(coveredSecs);
    }
    else
    {
        coverage = QStringLiteral("Channel buffer is empty.");
    }
    if (plan.lookbackExceedsBuffer)
    {
        coverage +=
            QStringLiteral(
                "  ⚠ Requested lookback exceeds buffered history; only the "
                "messages inside the buffer can be affected.");
    }
    this->previewCoverage_->setText(coverage);

    // Target list.
    QStringList lines;
    if (this->currentAction() == NukeAction::Delete)
    {
        for (const auto &id : plan.messageIds)
        {
            lines << id;
        }
        if (lines.isEmpty())
        {
            lines << QStringLiteral("(no matching messages with ids)");
        }
    }
    else
    {
        for (const auto &t : plan.targets)
        {
            lines << QStringLiteral("%1  —  %2 matched message(s)")
                         .arg(t.displayName)
                         .arg(t.matchedMessages);
        }
        if (lines.isEmpty())
        {
            lines << QStringLiteral("(no targets)");
        }
    }
    if (this->currentAction() == NukeAction::DeleteAndTimeout)
    {
        lines << QString();
        lines << QStringLiteral("Message IDs to delete: %1")
                     .arg(plan.messageIds.size());
    }
    this->previewTargets_->setPlainText(lines.join(QLatin1Char('\n')));

    // Execute only after a preview that produced something to do.
    const bool hasWork = this->currentAction() == NukeAction::Delete
                             ? !plan.messageIds.isEmpty()
                             : !plan.targets.isEmpty();
    this->executeButton_->setEnabled(hasWork && !this->executor_);
}

void LimerinoNukeDialog::onExecute()
{
    if (!this->channel_ || this->executor_)
    {
        return;
    }

    // Re-derive the plan at execution time so we act on the freshest buffer.
    const auto plan = buildPlan(this->channel_->getMessageSnapshot(),
                                this->channel_->messagePlatform(),
                                this->channel_->getName(),
                                nukeSelfLogin(*this->channel_),
                                this->contentMatcher(), this->senderMatcher(),
                                this->lookbackSpin_->value(),
                                this->currentAction());

    if (plan.targets.isEmpty() && plan.messageIds.isEmpty())
    {
        this->previewSummary_->setText(
            QStringLiteral("Nothing to do (plan is empty)."));
        return;
    }

    this->executor_ = new NukeExecutor(this->channel_, plan,
                                       this->currentAction(),
                                       this->timeoutSpin_->value(),
                                       this->reasonEdit_->text().trimmed(),
                                       this);

    QObject::connect(this->executor_, &NukeExecutor::progress, this,
                     &LimerinoNukeDialog::onProgress);
    QObject::connect(this->executor_, &NukeExecutor::finished, this,
                     &LimerinoNukeDialog::onFinished);

    this->executeButton_->setEnabled(false);
    this->previewButton_->setEnabled(false);
    this->cancelButton_->setVisible(true);
    this->progressBar_->setVisible(true);
    this->progressBar_->setRange(0, plan.targets.size() +
                                        plan.messageIds.size());
    this->resultsView_->clear();

    this->executor_->start();
}

void LimerinoNukeDialog::onCancel()
{
    if (this->executor_)
    {
        this->executor_->cancel();
    }
}

void LimerinoNukeDialog::onProgress(int done, int total, int succeeded,
                                    int failed)
{
    this->progressBar_->setMaximum(std::max(1, total));
    this->progressBar_->setValue(done);
    this->progressLabel_->setText(
        QStringLiteral("%1 / %2 done — %3 succeeded, %4 failed.")
            .arg(done)
            .arg(total)
            .arg(succeeded)
            .arg(failed));
}

void LimerinoNukeDialog::onFinished(bool cancelled,
                                    const QStringList &failures)
{
    this->cancelButton_->setVisible(false);
    this->previewButton_->setEnabled(true);

    this->progressLabel_->setText(
        cancelled
            ? QStringLiteral("Cancelled after %1 succeeded, %2 failed.")
                  .arg(this->progressBar_->value())
                  .arg(failures.size())
            : QStringLiteral("Done. %1 failed.").arg(failures.size()));

    if (!failures.isEmpty())
    {
        this->resultsView_->appendPlainText(
            QStringLiteral("--- failures ---"));
        for (const auto &f : failures)
        {
            this->resultsView_->appendPlainText(f);
        }
    }

    if (this->executor_)
    {
        this->executor_->deleteLater();
        this->executor_.clear();
    }

    // Offer Undo whenever this channel recorded a just-completed nuke.
    const auto &last = NukeExecutor::lastRun();
    const bool undoAvailable =
        last.has_value() &&
        last->channelName.compare(this->channel_->getName(),
                                  Qt::CaseInsensitive) == 0 &&
        !last->succeeded.isEmpty();
    this->undoButton_->setVisible(undoAvailable);
}

// --- presets (batch N4) --------------------------------------------------------

void LimerinoNukeDialog::refreshPresetList()
{
    this->presetList_->clear();
    for (const auto &p : loadNukePresets())
    {
        this->presetList_->addItem(p.name);
    }
}

void LimerinoNukeDialog::onPresetSelected(int row)
{
    const auto presets = loadNukePresets();
    if (row < 0 || row >= presets.size())
    {
        return;
    }
    this->applyPreset(presets[row]);
}

void LimerinoNukeDialog::onPresetSave()
{
    bool ok = false;
    const auto name =
        QInputDialog::getText(this, QStringLiteral("Save preset"),
                              QStringLiteral("Preset name:"),
                              QLineEdit::Normal, {}, &ok);
    if (!ok || name.trimmed().isEmpty())
    {
        return;
    }

    LimerinoNukePreset p;
    p.name = name.trimmed();
    p.content = this->contentMatcher();
    p.sender = this->senderMatcher();
    p.lookbackSeconds = this->lookbackSpin_->value();
    p.action = this->currentAction();
    p.timeoutSeconds = this->timeoutSpin_->value();
    p.reason = this->reasonEdit_->text().trimmed();

    upsertNukePreset(p);
    this->refreshPresetList();
    // Reselect the row we just wrote so it is obvious what was saved.
    for (int i = 0; i < this->presetList_->count(); ++i)
    {
        if (this->presetList_->item(i)->text().compare(
                p.name, Qt::CaseInsensitive) == 0)
        {
            this->presetList_->setCurrentRow(i);
            break;
        }
    }
}

void LimerinoNukeDialog::onPresetDelete()
{
    const int row = this->presetList_->currentRow();
    if (row < 0)
    {
        return;
    }
    const auto presets = loadNukePresets();
    if (row >= presets.size())
    {
        return;
    }
    const auto name = presets[row].name;

    const auto choice =
        QMessageBox::question(this, QStringLiteral("Delete preset"),
                              QStringLiteral("Delete preset \"%1\"?").arg(name));
    if (choice != QMessageBox::Yes)
    {
        return;
    }

    eraseNukePreset(name);
    this->refreshPresetList();
}

void LimerinoNukeDialog::applyPreset(const LimerinoNukePreset &preset)
{
    this->contentEdit_->setText(preset.content.pattern);
    this->contentRegex_->setChecked(preset.content.isRegex);
    this->contentCase_->setChecked(preset.content.caseSensitive);

    this->senderEdit_->setText(preset.sender.pattern);
    this->senderRegex_->setChecked(preset.sender.isRegex);
    this->senderCase_->setChecked(preset.sender.caseSensitive);

    this->lookbackSpin_->setValue(preset.lookbackSeconds);
    this->timeoutSpin_->setValue(preset.timeoutSeconds);
    this->reasonEdit_->setText(preset.reason);

    const int idx = this->actionCombo_->findData(
        static_cast<int>(preset.action));
    if (idx >= 0)
    {
        this->actionCombo_->setCurrentIndex(idx);
    }

    // Loading a preset invalidates any prior preview.
    this->onFieldChanged();
}

void LimerinoNukeDialog::onUndoLast()
{
    const QString outcome = undoLastNuke(this->channel_);
    this->resultsView_->appendPlainText(outcome);
    this->undoButton_->setVisible(false);
}

}  // namespace chatterino::limerino
