// SPDX-License-Identifier: MIT
// Per-event-type visibility filter for the "/events" channel.

#pragma once

#include "widgets/BasePopup.hpp"

class QVBoxLayout;

namespace chatterino::limerino {

class LimerinoEventFilterDialog : public BasePopup
{
    Q_OBJECT

public:
    explicit LimerinoEventFilterDialog(QWidget *parent = nullptr);

private:
    void rebuild();

    QVBoxLayout *listLayout_ = nullptr;
};

}  // namespace chatterino::limerino
