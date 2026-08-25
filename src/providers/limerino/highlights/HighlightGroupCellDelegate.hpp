// SPDX-License-Identifier: MIT
// Table-cell delegate rendering a combobox of highlight groups with a
// trailing "New group…" entry. Choosing a real group assigns the row's
// groupId; choosing "New group…" opens the group manager dialog, lets the
// user create the group inline, and on success assigns the new group.

#pragma once

#include "providers/limerino/highlights/HighlightGroup.hpp"

#include <QStyledItemDelegate>
#include <QUuid>

#include <vector>

namespace chatterino::limerino {

class HighlightGroupCellDelegate : public QStyledItemDelegate
{
    Q_OBJECT

public:
    explicit HighlightGroupCellDelegate(QObject *parent = nullptr);

    QWidget *createEditor(QWidget *parent, const QStyleOptionViewItem &option,
                          const QModelIndex &index) const override;

    void setEditorData(QWidget *editor, const QModelIndex &index) const override;

    void setModelData(QWidget *editor, QAbstractItemModel *model,
                      const QModelIndex &index) const override;

    /// Text role used to pass the list of "group names" (without "New
    /// group…") into createEditor().
    static constexpr int DisplayNamesRole = Qt::UserRole + 10;

    /// Text role used to store comma-of-group-id-list into createEditor().
    static constexpr int GroupIdsRole = Qt::UserRole + 11;

    /// Role used to mark a cell as being on a pinned (built-in) row; those
    /// cells are blank and never editable.
    static constexpr int IsPinnedRowRole = Qt::UserRole + 12;

private:
    struct Entry {
        QString text;
        QUuid groupId;  // null for the "New group…" sentinel
    };

    std::vector<Entry> entries(const QModelIndex &index) const;
};

}  // namespace chatterino::limerino
