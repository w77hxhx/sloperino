// SPDX-License-Identifier: MIT
// The one place UserInfoPopup is hooked: a per-card extras widget mounted into
// the head-right of the upper half. Holds the GQL extras fetch (language tag;
// team + subscription joined onto the surrounding rows in G3/G4), the
// QPointer+generation teardown guard, and all rendering logic. UserInfoPopup
// itself stays a one-line constructor daughter plus one setTarget() call.

#pragma once

#include "providers/limerino/gql/LimerinoUserCardExtras.hpp"
#include "widgets/BaseWidget.hpp"

#include <QString>

#include <optional>

namespace chatterino {
class Label;

namespace limerino {

class LimerinoUserCardWidget final : public BaseWidget
{
    Q_OBJECT

public:
    explicit LimerinoUserCardWidget(QWidget *parent = nullptr);

    // Hand over which user / channel this card is pointing at now.
    // Empty userId (or a Kick-prefixed one) clears and hides everything -
    // this widget exists for Twitch usercards only.
    void setTarget(const QString &userId, const QString &channelId,
                   const QString &loginForSubRow);

    // G4: detail string the popup appends onto its existing sub-age row.
    // Empty whenever nothing extra is known (null detail or all sub-fields
    // absent) - caller shows nothing in that case.
    QString subscriptionSuffix() const;

Q_SIGNALS:
    // Fired whenever extras_ changes (fetch succeeded or target cleared).
    // The popup re-appends subscriptionSuffix() to its sub-age row on this.
    void extrasChanged();

private:
    void refetch();
    void rebuild();

    QString userId_;
    QString channelId_;
    QString loginForSubRow_;

    LimerinoAuth::gql::LimerinoUserCardExtras extras_;
    quint64 requestGeneration_{0};

    Label *languageTagLabel_ = nullptr;  // G2: language tag, top-right
    Label *teamLabel_ = nullptr;         // G3: primary team
    Label *subDetailLabel_ = nullptr;    // G4: platform/Prime/gift detail
};

}  // namespace limerino
}  // namespace chatterino
