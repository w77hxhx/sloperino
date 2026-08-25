// SPDX-License-Identifier: MIT
// Appearance page of the channel-points window: pick global badge, chat color.
// - available badges: ChatSettings_Badges (persisted op, channel context)
// - apply badge: ChatSettings_SelectGlobalBadge (persisted op, plugin fields verbatim)
// - chat color: native `updateUserChatColor` (same call the stock /color command runs)
// Per-channel badge mutation doesn't exist in any verified API; the scope
// switch shows that plainly instead of pretending.

#pragma once

#include "widgets/BaseWidget.hpp"

#include <QString>

class QComboBox;
class QLabel;
class QPushButton;
class QWidget;

namespace chatterino {
class ColorButton;
class Split;
class TwitchChannel;

namespace limerino {

class LimerinoColorField;

class LimerinoAppearanceWidget final : public BaseWidget
{
    Q_OBJECT

public:
    explicit LimerinoAppearanceWidget(Split *split);

private:
    void refreshBadges();
    void refreshPreview();
    void rebuildRecentSwatches();
    void selectColorValue(const QString &value, bool fromCustomField);
    void applyBadge();
    void applyColor();

    Split *split_ = nullptr;

    QComboBox *badgeCombo_ = nullptr;
    QComboBox *scopeCombo_ = nullptr;
    QLabel *previewLabel_ = nullptr;

    QWidget *namedSwatchRow_ = nullptr;
    QWidget *recentSwatchRow_ = nullptr;
    LimerinoColorField *customColorField_ = nullptr;
    QLabel *colorPreviewLabel_ = nullptr;
    QPushButton *applyColorButton_ = nullptr;

    /// Helix name or #hex currently selected for Apply / preview.
    QString selectedColorValue_;

    QPushButton *applyBadgeButton_ = nullptr;
    QLabel *statusLabel_ = nullptr;
};

}  // namespace limerino
}  // namespace chatterino
