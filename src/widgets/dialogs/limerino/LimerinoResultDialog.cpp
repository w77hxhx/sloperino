// SPDX-License-Identifier: MIT

#include "widgets/dialogs/limerino/LimerinoResultDialog.hpp"

#include "widgets/dialogs/limerino/LimerinoResultList.hpp"

#include <QVBoxLayout>

namespace chatterino::limerino {

LimerinoResultDialog::LimerinoResultDialog(QWidget *parent)
    : BasePopup({BaseWindow::Flags::Dialog}, parent)
{
    this->list_ = new LimerinoResultList(this);
    auto *root = new QVBoxLayout(this);
    root->addWidget(this->list_);
    this->resize(480, 420);
}

LimerinoResultList *LimerinoResultDialog::resultList() const
{
    return this->list_;
}

}  // namespace chatterino::limerino
