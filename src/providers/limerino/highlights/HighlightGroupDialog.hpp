// SPDX-License-Identifier: MIT
// Group manager dialog for per-channel highlight groups (H3).
//   * List of groups on the left (Default undeletable; highlighted).
//   * Right side: scope radio (Everywhere vs "All except:" vs "Only:") plus
//     a channel-list editor with autocomplete sourced from currently-open
//     and recently-joined channels.
//   * A visible warning marker on any entry not currently known to the
//     client (so typos are loud, not silent).
//   * Deleting a group asks for confirmation and reassigns highlights
//     belonging to it back to Default.

#pragma once

#include "providers/limerino/highlights/HighlightGroup.hpp"

#include <QDialog>
#include <QUuid>

class QListWidget;
class QListWidgetItem;
class QLineEdit;
class QRadioButton;
class QLabel;
class QVBoxLayout;

namespace chatterino::limerino {

class HighlightGroupDialog : public QDialog
{
    Q_OBJECT

public:
    explicit HighlightGroupDialog(QWidget *parent = nullptr);

    /// Opens this dialog centered on the parent, creating a *new* group
    /// (random UUID) so the caller can edit it and press OK to persist.
    /// Returns the new group's QUuid on success, or a null UUID if the user
    /// cancelled.
    static QUuid createGroupModal(QWidget *parent);

private:
    void rebuildGroupList(const QUuid &selectId = {});
    void onSelectionChanged();
    void onAddClicked();
    void onDeleteClicked();

    void refreshScopePanel();
    void applyScopePanel();
    void addChannelText(const QString &rawText);

    HighlightGroup load(const QUuid &id) const;
    void store(const HighlightGroup &group);

    /// Return the number of highlights (phrase + user + badge) that would be
    /// moved to the Default group if the given group were deleted.
    int countMembers(const QUuid &groupId) const;

    QListWidget *groupList_{};
    QLineEdit *nameEdit_{};
    QRadioButton *radioEverywhere_{};
    QRadioButton *radioAllExcept_{};
    QRadioButton *radioOnly_{};
    QLineEdit *channelInput_{};
    QListWidget *channelList_{};
    QLabel *memberCountLabel_{};
    QLabel *deleteHint_{};
    QVBoxLayout *warningPillsLayout_{};

    // Cached copy of the currently-edited group. The settings SignalVector is
    // only written to on Apply / OK / explicit mutation.
    QUuid currentId_;
    HighlightGroup::Scope currentScope_ = HighlightGroup::Scope::AllExcept;
    QStringList currentChannels_;

    bool suppressSelectionSignals_ = false;
};

}  // namespace chatterino::limerino
