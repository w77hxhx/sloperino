// SPDX-License-Identifier: MIT
// Reusable sortable/paginated result table for Limerino command surfaces.
// Shape: title row, table with two columns (Key / Value) by default, status
// line at the bottom, per-row "Copy" action, 25 rows per page.
// Consumers: batch 1 (/modlist), 2 (follows), 5 (history), 9 (roles), 10 (modlogs).

#pragma once

#include <QWidget>

#include <QSet>

class QLabel;
class QLineEdit;
class QPushButton;
class QTableWidget;

#include <functional>

class QMenu;

namespace chatterino::limerino {

class LimerinoResultList : public QWidget
{
    Q_OBJECT

public:
    // Optional per-row context menu: consumer receives the row's column values
    // and appends its own actions (a "Copy row" action is always appended).
    using RowMenuProvider =
        std::function<void(const QStringList &row, QMenu *menu)>;

    explicit LimerinoResultList(QWidget *parent = nullptr);

    void setTitleText(const QString &title);
    void setColumns(const QStringList &headers);
    void setRows(const QVector<QStringList> &rows);
    void setStatusText(const QString &status);
    void clear();

    // Show a live search box that filters rows (any column, case-insensitive).
    void enableSearch(bool enabled);
    void setRowMenuProvider(RowMenuProvider provider);

    // Optional: name click resolves to a URL to open instead of copying.
    void setRowOpenUrlProvider(
        std::function<QString(const QStringList &row)> provider);

    // Columns that should sort numerically (default: lexicographic).
    void setNumericColumns(const QSet<int> &columns);

private:
    void applyPage();
    void copyCell(int row, int column);
    void refreshFilter();

    static constexpr int PAGE_SIZE = 25;

    QLabel *titleLabel_ = nullptr;
    QLineEdit *filterEdit_ = nullptr;
    QTableWidget *table_ = nullptr;
    QLabel *statusLabel_ = nullptr;
    QPushButton *prevButton_ = nullptr;
    QPushButton *nextButton_ = nullptr;
    QLabel *pageLabel_ = nullptr;

    QStringList headers_;
    QVector<QStringList> rows_;
    QVector<QStringList> filteredRows_;
    int page_ = 0;
    RowMenuProvider rowMenuProvider_{};
    std::function<QString(const QStringList &)> rowOpenUrlProvider_{};
    QSet<int> numericColumns_;
};

}  // namespace chatterino::limerino
