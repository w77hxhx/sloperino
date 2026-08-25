// SPDX-License-Identifier: MIT
// Settings-page feature wiki: renders entirely from the catalog in
// src/limerino/LimerinoFeatures.cpp (enforced by tests/src/LimerinoFeatures.cpp).
// Grouped by category, collapsed by default, live filter box, status badge
// per entry in theme colors.

#pragma once

#include "widgets/BaseWidget.hpp"

#include <QSet>
#include <QString>

class QLineEdit;
class QVBoxLayout;

namespace chatterino::limerino {

class LimerinoWikiWidget final : public BaseWidget
{
    Q_OBJECT

public:
    explicit LimerinoWikiWidget(QWidget *parent = nullptr);

private:
    void rebuild();

    QLineEdit *filterEdit_ = nullptr;
    QVBoxLayout *listLayout_ = nullptr;

    /// Expanded entries survive filter rebuilds.
    QSet<QString> expandedIds_;
};

}  // namespace chatterino::limerino
