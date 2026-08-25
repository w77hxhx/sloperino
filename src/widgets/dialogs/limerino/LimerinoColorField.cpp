// SPDX-License-Identifier: MIT

#include "widgets/dialogs/limerino/LimerinoColorField.hpp"

#include "widgets/dialogs/ColorPickerDialog.hpp"
#include "widgets/helper/color/ColorButton.hpp"

#include <QAbstractButton>
#include <QHBoxLayout>
#include <QLineEdit>
#include <QSignalBlocker>

namespace chatterino::limerino {

namespace {

QString hexForDisplay(const QColor &c)
{
    return c.alpha() < 255 ? c.name(QColor::HexArgb) : c.name(QColor::HexRgb);
}

}  // namespace

LimerinoColorField::LimerinoColorField(QWidget *parent)
    : QWidget(parent)
    , color_(Qt::white)
{
    auto *row = new QHBoxLayout(this);
    row->setContentsMargins(0, 0, 0, 0);

    this->swatch_ = new ColorButton(this->color_, this);
    this->swatch_->setFixedSize(40, 24);
    this->swatch_->setMinimumSize(40, 24);
    this->swatch_->setToolTip(QStringLiteral("Pick color"));
    QObject::connect(this->swatch_, &QAbstractButton::clicked, this,
                     &LimerinoColorField::openPicker);

    this->hex_ = new QLineEdit(this);
    this->hex_->setPlaceholderText(QStringLiteral("#RGB / #RRGGBB / #AARRGGBB"));
    this->hex_->setMaximumWidth(140);
    QObject::connect(this->hex_, &QLineEdit::textEdited, this,
                     &LimerinoColorField::onHexEdited);

    row->addWidget(this->swatch_);
    row->addWidget(this->hex_);
    row->addStretch(1);

    this->hex_->setText(hexForDisplay(this->color_));
}

QColor LimerinoColorField::color() const
{
    return this->color_;
}

void LimerinoColorField::setColor(const QColor &color, bool updateHex)
{
    this->applyColor(color, updateHex, false);
}

void LimerinoColorField::applyColor(const QColor &color, bool updateHex,
                                    bool emitSignal)
{
    if (!color.isValid())
    {
        return;
    }
    this->color_ = color;
    this->swatch_->setColor(color);
    if (updateHex)
    {
        const QSignalBlocker block(this->hex_);
        this->hex_->setText(hexForDisplay(color));
    }
    if (emitSignal)
    {
        Q_EMIT this->colorChanged(color);
    }
}

void LimerinoColorField::openPicker()
{
    this->colorBeforePicker_ = this->color_;
    this->pickerConfirmed_ = false;

    auto *dialog = new ColorPickerDialog(this->color_, this);
    QObject::connect(dialog, &ColorPickerDialog::colorChanged, this,
                     [this](QColor color) {
                         this->applyColor(color, true, true);
                     });
    QObject::connect(dialog, &ColorPickerDialog::colorConfirmed, this,
                     [this](QColor color) {
                         this->pickerConfirmed_ = true;
                         this->applyColor(color, true, true);
                     });
    QObject::connect(dialog, &QObject::destroyed, this, [this] {
        if (!this->pickerConfirmed_)
        {
            this->applyColor(this->colorBeforePicker_, true, true);
        }
    });
    dialog->show();
}

void LimerinoColorField::onHexEdited()
{
    const QColor parsed(this->hex_->text().trimmed());
    if (!parsed.isValid())
    {
        return;
    }
    this->applyColor(parsed, false, true);
}

}  // namespace chatterino::limerino
