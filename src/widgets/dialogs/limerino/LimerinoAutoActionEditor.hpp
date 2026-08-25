// SPDX-License-Identifier: MIT
// Per-rule editor for auto actions (batch N7).
//
// One dialog per rule. Fields: name, enable, content/sender matchers (with
// live regex validation), action string (with placeholder reference pane),
// cooldown, and channel scope (reusing the HighlightGroup channels editor's
// normalisation). A Test box runs the rule against a pasted sample message.

#pragma once

#include "widgets/BasePopup.hpp"

#include <QPointer>
#include <QUuid>

class QCheckBox;
class QComboBox;
class QLabel;
class QLineEdit;
class QListWidget;
class QPlainTextEdit;
class QPushButton;
class QSpinBox;
class QVBoxLayout;

namespace chatterino::limerino {

struct LimerinoAutoAction;

class LimerinoAutoActionEditor : public BasePopup
{
    Q_OBJECT

public:
    /// Edit an existing rule (id kept), or create a new one
    /// (`existing.isNull()`).
    explicit LimerinoAutoActionEditor(QWidget *parent,
                                      const LimerinoAutoAction &existing);

Q_SIGNALS:
    void ruleSaved(const LimerinoAutoAction &rule);

private:
    void onSave();
    void rebuildValidation();
    void onTest();

    void addActionRow(QVBoxLayout *layout);

    QUuid id_;

    QLineEdit *nameEdit_ = nullptr;
    QCheckBox *enabledCheck_ = nullptr;

    QLineEdit *contentEdit_ = nullptr;
    QCheckBox *contentRegex_ = nullptr;
    QCheckBox *contentCase_ = nullptr;
    QLabel *contentError_ = nullptr;

    QLineEdit *senderEdit_ = nullptr;
    QCheckBox *senderRegex_ = nullptr;
    QCheckBox *senderCase_ = nullptr;
    QLabel *senderError_ = nullptr;

    QLineEdit *actionEdit_ = nullptr;
    QLabel *actionError_ = nullptr;
    QPlainTextEdit *placeholderHelp_ = nullptr;

    QComboBox *scopeCombo_ = nullptr;
    QLineEdit *channelsEdit_ = nullptr;
    QSpinBox *cooldownSpin_ = nullptr;

    // Test area
    QLineEdit *testChannel_ = nullptr;
    QLineEdit *testSender_ = nullptr;
    QPlainTextEdit *testMessage_ = nullptr;
    QPlainTextEdit *testResult_ = nullptr;

    QPushButton *saveButton_ = nullptr;
};

}  // namespace chatterino::limerino
