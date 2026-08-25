// SPDX-License-Identifier: MIT
// Thin BasePopup shell hosting a LimerinoResultList - the dialog surface for
// generated list data (/modlist, name history, follows, ...).

#pragma once

#include "widgets/BasePopup.hpp"

namespace chatterino::limerino {

class LimerinoResultList;

class LimerinoResultDialog : public BasePopup
{
    Q_OBJECT

public:
    explicit LimerinoResultDialog(QWidget *parent = nullptr);

    LimerinoResultList *resultList() const;

private:
    LimerinoResultList *list_ = nullptr;
};

}  // namespace chatterino::limerino
