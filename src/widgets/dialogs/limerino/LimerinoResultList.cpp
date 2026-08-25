// SPDX-License-Identifier: MIT

#include "widgets/dialogs/limerino/LimerinoResultList.hpp"

#include "util/Clipboard.hpp"

#include <QHeaderView>
#include <QLabel>
#include <QLineEdit>
#include <QMenu>
#include <QPushButton>
#include <QTableWidget>
#include <QTableWidgetItem>
#include <QDesktopServices>
#include <QUrl>
#include <QVBoxLayout>

#include <algorithm>

namespace chatterino::limerino {

namespace {

// Numeric-aware cell for columns declared numeric via setNumericColumns().
class NumericTableWidgetItem : public QTableWidgetItem
{
public:
    using QTableWidgetItem::QTableWidgetItem;

    bool operator<(const QTableWidgetItem &other) const override
    {
        bool okA = false;
        bool okB = false;
        const double a = this->text().toDouble(&okA);
        const double b = other.text().toDouble(&okB);
        if (okA && okB)
        {
            return a < b;
        }
        return QTableWidgetItem::operator<(other);
    }
};

}  // namespace

LimerinoResultList::LimerinoResultList(QWidget *parent)
    : QWidget(parent)
{
    auto *root = new QVBoxLayout(this);

    this->titleLabel_ = new QLabel(this);
    this->titleLabel_->setWordWrap(true);
    root->addWidget(this->titleLabel_);

    this->filterEdit_ = new QLineEdit(this);
    this->filterEdit_->setPlaceholderText(QStringLiteral("filter..."));
    this->filterEdit_->setVisible(false);
    root->addWidget(this->filterEdit_);

    this->table_ = new QTableWidget(this);
    this->table_->setEditTriggers(QAbstractItemView::NoEditTriggers);
    this->table_->setSelectionMode(QAbstractItemView::NoSelection);
    this->table_->setFocusPolicy(Qt::NoFocus);
    this->table_->setAlternatingRowColors(true);
    this->table_->verticalHeader()->setVisible(false);
    this->table_->setSortingEnabled(true);
    this->table_->setContextMenuPolicy(Qt::CustomContextMenu);
    root->addWidget(this->table_, 1);

    auto *pager = new QHBoxLayout;
    this->statusLabel_ = new QLabel(this);
    pager->addWidget(this->statusLabel_, 1);
    this->prevButton_ = new QPushButton(QStringLiteral("<"), this);
    this->nextButton_ = new QPushButton(QStringLiteral(">"), this);
    this->pageLabel_ = new QLabel(this);
    pager->addWidget(this->prevButton_);
    pager->addWidget(this->pageLabel_);
    pager->addWidget(this->nextButton_);
    root->addLayout(pager);

    QObject::connect(this->prevButton_, &QPushButton::clicked, this, [this] {
        if (this->page_ > 0)
        {
            --this->page_;
            this->applyPage();
        }
    });
    QObject::connect(this->nextButton_, &QPushButton::clicked, this, [this] {
        const int maxPage =
            std::max(0, int((this->filteredRows_.size() - 1) / PAGE_SIZE));
        if (this->page_ < maxPage)
        {
            ++this->page_;
            this->applyPage();
        }
    });
    QObject::connect(this->filterEdit_, &QLineEdit::textChanged, this,
                     [this] { this->refreshFilter(); });

    QObject::connect(this->table_, &QTableWidget::cellActivated, this,
                     [this](int row, int column) {
                         if (this->rowOpenUrlProvider_)
                         {
                             QStringList cells;
                             for (int c = 0; c < this->headers_.size(); ++c)
                             {
                                 auto *item = this->table_->item(row, c);
                                 cells.append(item != nullptr ? item->text()
                                                              : QString());
                             }
                             const QString url = this->rowOpenUrlProvider_(cells);
                             if (!url.isEmpty())
                             {
                                 QDesktopServices::openUrl(QUrl(url));
                                 return;
                             }
                         }
                         this->copyCell(row, column);
                     });

    QObject::connect(this->table_,
                     &QTableWidget::customContextMenuRequested, this,
                     [this](const QPoint &pos) {
                         const int row = this->table_->rowAt(pos.y());
                         if (row < 0)
                         {
                             return;
                         }
                         QStringList cells;
                         for (int c = 0; c < this->headers_.size(); ++c)
                         {
                             auto *item = this->table_->item(row, c);
                             cells.append(item != nullptr ? item->text()
                                                          : QString());
                         }

                         QMenu menu(this);
                         if (this->rowMenuProvider_)
                         {
                             this->rowMenuProvider_(cells, &menu);
                         }
                         menu.addAction(QStringLiteral("Copy row"), [cells] {
                             crossPlatformCopy(
                                 cells.join(QStringLiteral("\t")));
                         });
                         if (!menu.isEmpty())
                         {
                             menu.exec(this->table_->viewport()->mapToGlobal(
                                 pos));
                         }
                     });
}

void LimerinoResultList::enableSearch(bool enabled)
{
    this->filterEdit_->setVisible(enabled);
}

void LimerinoResultList::setRowMenuProvider(RowMenuProvider provider)
{
    this->rowMenuProvider_ = std::move(provider);
}

void LimerinoResultList::setNumericColumns(const QSet<int> &columns)
{
    this->numericColumns_ = columns;
}

void LimerinoResultList::setRowOpenUrlProvider(
    std::function<QString(const QStringList &)> provider)
{
    this->rowOpenUrlProvider_ = std::move(provider);
}

void LimerinoResultList::refreshFilter()
{
    const QString filter = this->filterEdit_->text();
    this->filteredRows_.clear();
    if (filter.isEmpty())
    {
        this->filteredRows_ = this->rows_;
    }
    else
    {
        for (const QStringList &row : this->rows_)
        {
            for (const QString &cell : row)
            {
                if (cell.contains(filter, Qt::CaseInsensitive))
                {
                    this->filteredRows_.append(row);
                    break;
                }
            }
        }
    }
    this->page_ = 0;
    this->applyPage();
}

void LimerinoResultList::setTitleText(const QString &title)
{
    this->titleLabel_->setText(title);
}

void LimerinoResultList::setColumns(const QStringList &headers)
{
    this->headers_ = headers;
    this->table_->setColumnCount(int(headers.size()) + 1);  // +1: copy column
    QStringList withCopy(headers);
    withCopy.append(QString());
    this->table_->setHorizontalHeaderLabels(withCopy);
    for (int i = 0; i < headers.size(); ++i)
    {
        this->table_->horizontalHeader()->setSectionResizeMode(
            i, i == 1 ? QHeaderView::Stretch : QHeaderView::ResizeToContents);
    }
}

void LimerinoResultList::setRows(const QVector<QStringList> &rows)
{
    this->rows_ = rows;
    this->refreshFilter();
}

void LimerinoResultList::setStatusText(const QString &status)
{
    this->statusLabel_->setText(status);
}

void LimerinoResultList::clear()
{
    this->rows_.clear();
    this->filteredRows_.clear();
    this->page_ = 0;
    this->table_->setRowCount(0);
    this->pageLabel_->clear();
}

void LimerinoResultList::applyPage()
{
    this->table_->setSortingEnabled(false);
    this->table_->setRowCount(0);

    const int start = this->page_ * PAGE_SIZE;
    const int end =
        std::min(int(this->filteredRows_.size()), start + PAGE_SIZE);
    this->table_->setRowCount(std::max(0, end - start));

    for (int r = start; r < end; ++r)
    {
        const QStringList &cols = this->filteredRows_.at(r);
        for (int c = 0; c < cols.size() && c < this->headers_.size(); ++c)
        {
            QTableWidgetItem *item;
            if (this->numericColumns_.contains(c))
            {
                item = new NumericTableWidgetItem(cols.at(c));
            }
            else
            {
                item = new QTableWidgetItem(cols.at(c));
            }
            this->table_->setItem(r - start, c, item);
        }

        auto *copyButton = new QPushButton(QStringLiteral("copy"), this->table_);
        const QString joined = cols.join(QStringLiteral("\t"));
        QObject::connect(copyButton, &QPushButton::clicked, this, [joined] {
            crossPlatformCopy(joined);
        });
        this->table_->setCellWidget(r - start, int(this->headers_.size()),
                                    copyButton);
    }

    const int maxPage =
        std::max(0, int((this->filteredRows_.size() - 1) / PAGE_SIZE));
    this->pageLabel_->setText(QStringLiteral("%1 / %2 (%3 rows)")
                                  .arg(this->page_ + 1)
                                  .arg(maxPage + 1)
                                  .arg(this->filteredRows_.size()));
    this->prevButton_->setEnabled(this->page_ > 0);
    this->nextButton_->setEnabled(this->page_ < maxPage);

    this->table_->setSortingEnabled(true);
}

void LimerinoResultList::copyCell(int row, int column)
{
    auto *item = this->table_->item(row, column);
    if (item != nullptr)
    {
        crossPlatformCopy(item->text());
    }
}

}  // namespace chatterino::limerino
