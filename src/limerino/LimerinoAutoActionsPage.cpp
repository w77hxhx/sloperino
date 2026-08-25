// SPDX-License-Identifier: MIT

#include "limerino/LimerinoAutoActionsPage.hpp"

#include "providers/limerino/autoactions/LimerinoAutoAction.hpp"
#include "providers/limerino/autoactions/LimerinoAutoActionStore.hpp"
#include "util/LayoutCreator.hpp"
#include "widgets/dialogs/limerino/LimerinoAutoActionEditor.hpp"

#include <QAbstractItemView>
#include <QAbstractScrollArea>
#include <QHBoxLayout>
#include <QLabel>
#include <QListWidget>
#include <QPushButton>
#include <QSizePolicy>
#include <QVBoxLayout>

namespace chatterino {

namespace {

// QListView::sizeHint follows the longest item. Settings uses a QStackedLayout
// that sizes to that hint, so long actions used to stretch the whole window.
class AutoActionRuleList : public QListWidget
{
public:
    explicit AutoActionRuleList(QWidget *parent = nullptr)
        : QListWidget(parent)
    {
        this->setHorizontalScrollBarPolicy(Qt::ScrollBarAsNeeded);
        this->setVerticalScrollBarPolicy(Qt::ScrollBarAsNeeded);
        this->setTextElideMode(Qt::ElideNone);
        this->setSizeAdjustPolicy(QAbstractScrollArea::AdjustIgnored);
        this->setSizePolicy(QSizePolicy::Ignored, QSizePolicy::Expanding);
        this->setMinimumWidth(0);
        this->setHorizontalScrollMode(QAbstractItemView::ScrollPerPixel);
    }

    QSize sizeHint() const override
    {
        return {1, 200};
    }

    QSize minimumSizeHint() const override
    {
        return {0, 80};
    }
};

}  // namespace

LimerinoAutoActionsPage::LimerinoAutoActionsPage()
{
    LayoutCreator<LimerinoAutoActionsPage> layoutCreator(this);
    auto layout = layoutCreator.setLayoutType<QVBoxLayout>();

    auto *intro = layout
                      .emplace<QLabel>(QStringLiteral(
                          "Rules that run automatically when a message arrives. "
                          "A rule matches message text and/or sender (regex or "
                          "plain text, case-insensitive by default). The action "
                          "string supports placeholders like {sender.name}, "
                          "{msg.id}, {channel.name}."))
                      .getElement();
    intro->setWordWrap(true);

    this->list_ = layout.emplace<AutoActionRuleList>().getElement();

    auto *buttonRow = layout.emplace<QHBoxLayout>().getElement();
    buttonRow->setContentsMargins(0, 0, 0, 0);
    this->addButton_ = new QPushButton(QStringLiteral("Add rule..."));
    this->editButton_ = new QPushButton(QStringLiteral("Edit..."));
    this->deleteButton_ = new QPushButton(QStringLiteral("Delete"));
    buttonRow->addWidget(this->addButton_);
    buttonRow->addWidget(this->editButton_);
    buttonRow->addWidget(this->deleteButton_);
    buttonRow->addStretch(1);

    this->placeholderHelp_ = layout
                                 .emplace<QLabel>(QStringLiteral(
                                     "Placeholders: {msg.id} {sender.name} "
                                     "{sender.displayName} {sender.id} "
                                     "{channel.name} {channel.id} {platform}. "
                                     "Unknown placeholders expand to empty; an "
                                     "unavailable value skips the action "
                                     "entirely."))
                                 .getElement();
    this->placeholderHelp_->setWordWrap(true);
    this->placeholderHelp_->setStyleSheet(QStringLiteral("color: #bbb"));

    QObject::connect(this->addButton_, &QPushButton::clicked, this,
                     &LimerinoAutoActionsPage::onAdd);
    QObject::connect(this->editButton_, &QPushButton::clicked, this,
                     &LimerinoAutoActionsPage::onEdit);
    QObject::connect(this->deleteButton_, &QPushButton::clicked, this,
                     &LimerinoAutoActionsPage::onDelete);
    QObject::connect(this->list_, &QListWidget::itemDoubleClicked, this,
                     [this](QListWidgetItem *) {
                         this->onEdit();
                     });

    this->rebuildList();
}

void LimerinoAutoActionsPage::onShow()
{
    this->rebuildList();
}

bool LimerinoAutoActionsPage::filterElements(const QString &query)
{
    if (query.isEmpty())
    {
        for (int i = 0; i < this->list_->count(); ++i)
        {
            this->list_->item(i)->setHidden(false);
        }
        return true;
    }

    const auto needle = query.toLower();
    bool any = false;
    for (int i = 0; i < this->list_->count(); ++i)
    {
        auto *item = this->list_->item(i);
        const bool match = item->text().toLower().contains(needle);
        item->setHidden(!match);
        any = any || match;
    }
    return any;
}

void LimerinoAutoActionsPage::rebuildList()
{
    if (this->list_ == nullptr)
    {
        return;
    }

    const int previousRow = this->list_->currentRow();
    this->list_->clear();
    for (const auto &rule : limerino::loadLimerinoAutoActions())
    {
        const auto label =
            QStringLiteral("%1 %2%3")
                .arg(rule.enabled ? QStringLiteral("[on]")
                                  : QStringLiteral("[off]"))
                .arg(rule.name)
                .arg(rule.action.isEmpty()
                         ? QString()
                         : QStringLiteral("   ->   ") + rule.action);
        auto *item = new QListWidgetItem(label, this->list_);
        item->setData(Qt::UserRole, rule.id.toString(QUuid::WithoutBraces));
    }
    if (previousRow >= 0 && previousRow < this->list_->count())
    {
        this->list_->setCurrentRow(previousRow);
    }
}

void LimerinoAutoActionsPage::onAdd()
{
    limerino::LimerinoAutoAction blank;
    auto *dialog = new limerino::LimerinoAutoActionEditor(this, blank);
    dialog->setAttribute(Qt::WA_DeleteOnClose);
    QObject::connect(dialog, &limerino::LimerinoAutoActionEditor::ruleSaved,
                     this, [this](const limerino::LimerinoAutoAction &rule) {
                         limerino::addLimerinoAutoAction(rule);
                         this->rebuildList();
                     });
    dialog->show();
}

void LimerinoAutoActionsPage::onEdit()
{
    const int row = this->list_->currentRow();
    const auto rules = limerino::loadLimerinoAutoActions();
    if (row < 0 || row >= rules.size())
    {
        return;
    }
    auto *dialog =
        new limerino::LimerinoAutoActionEditor(this, rules[row]);
    dialog->setAttribute(Qt::WA_DeleteOnClose);
    QObject::connect(dialog, &limerino::LimerinoAutoActionEditor::ruleSaved,
                     this, [this](const limerino::LimerinoAutoAction &rule) {
                         limerino::updateLimerinoAutoAction(rule);
                         this->rebuildList();
                     });
    dialog->show();
}

void LimerinoAutoActionsPage::onDelete()
{
    const int row = this->list_->currentRow();
    const auto rules = limerino::loadLimerinoAutoActions();
    if (row < 0 || row >= rules.size())
    {
        return;
    }
    limerino::removeLimerinoAutoAction(rules[row].id);
    this->rebuildList();
}

}  // namespace chatterino
