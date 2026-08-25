// SPDX-License-Identifier: MIT
// Shared swatch + hex colour field used by Theme Creator and Appearance (E7).
// Opens the existing ColorPickerDialog; cancel restores the pre-picker colour.

#pragma once

#include <QColor>
#include <QWidget>

class QLineEdit;

namespace chatterino {

class ColorButton;

namespace limerino {

class LimerinoColorField : public QWidget
{
    Q_OBJECT

public:
    explicit LimerinoColorField(QWidget *parent = nullptr);

    QColor color() const;
    /// Updates swatch (and hex unless updateHex is false). Does not emit.
    void setColor(const QColor &color, bool updateHex = true);

Q_SIGNALS:
    /// Emitted on live picker drag, picker confirm, and valid hex edits.
    void colorChanged(QColor color);

private:
    void openPicker();
    void onHexEdited();
    void applyColor(const QColor &color, bool updateHex, bool emitSignal);

    ColorButton *swatch_ = nullptr;
    QLineEdit *hex_ = nullptr;
    QColor color_;
    QColor colorBeforePicker_;
    bool pickerConfirmed_ = false;
};

}  // namespace limerino
}  // namespace chatterino
