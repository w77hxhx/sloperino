// SPDX-License-Identifier: MIT
// Channel-points prediction window (batches 4+5), viewer/mod profiles:
// - everyone: live prediction status, "Make prediction" (points, confirmation),
//   past predictions list
// - mods: same + create form, draft history (last 5), lock, payout, refund
// GQL ops transcribed from pluginforreference/requests.lua.

#pragma once

#include "widgets/BasePopup.hpp"

#include <pajlada/signals/signalholder.hpp>

#include <QJsonObject>

class QComboBox;
class QGroupBox;
class QLabel;
class QLineEdit;
class QPushButton;
class QSpinBox;
class QVBoxLayout;

namespace chatterino {
class Split;
class TwitchChannel;

namespace limerino {

class LimerinoResultList;

class LimerinoPredictionDialog final : public BasePopup
{
    Q_OBJECT

public:
    explicit LimerinoPredictionDialog(Split *split);

private:
    void refreshContext();
    void applyModGating();
    void submitCreate();
    void addOptionRow(const QString &text = QString());
    void submitCreatePoll();
    void addPollOptionRow(const QString &text = QString());
    void appendDraft(const QString &title, const QStringList &options,
                     int windowSeconds);
    void refillFromDraft(int index);
    void makePrediction();
    void lockPrediction(const QString &eventId);
    void refundPrediction(const QString &eventId);
    void payoutPrediction(const QString &eventId, const QString &outcomeId);
    void refreshRewards();
    void redeemReward(const QString &channelId, const QJsonObject &reward,
                      const QString &textInput);
    void promptRedeem(const QString &channelId, const QJsonObject &reward);

    Split *split_ = nullptr;

    // create (mod)
    QGroupBox *createBox_ = nullptr;
    QLineEdit *titleEdit_ = nullptr;
    QSpinBox *windowSpin_ = nullptr;
    QVBoxLayout *optionsLayout_ = nullptr;
    QPushButton *addOptionButton_ = nullptr;
    QPushButton *createButton_ = nullptr;

    // create poll (mod)
    QGroupBox *createPollBox_ = nullptr;
    QLineEdit *pollTitleEdit_ = nullptr;
    QSpinBox *pollDurationSpin_ = nullptr;
    QVBoxLayout *pollOptionsLayout_ = nullptr;
    QPushButton *addPollOptionButton_ = nullptr;
    QPushButton *createPollButton_ = nullptr;

    // draft history (mod)
    QGroupBox *draftsBox_ = nullptr;
    QComboBox *draftsCombo_ = nullptr;
    QPushButton *draftsUseButton_ = nullptr;

    // active prediction (everyone)
    QLabel *activeLabel_ = nullptr;
    QVBoxLayout *activeLayout_ = nullptr;
    QComboBox *outcomeCombo_ = nullptr;
    QSpinBox *pointsSpin_ = nullptr;
    QPushButton *makeButton_ = nullptr;
    QPushButton *lockButton_ = nullptr;
    QPushButton *refundButton_ = nullptr;
    QPushButton *refreshButton_ = nullptr;

    // past predictions (everyone)
    LimerinoResultList *pastList_ = nullptr;

    // channel point rewards (everyone)
    QLabel *balanceLabel_ = nullptr;
    LimerinoResultList *rewardsList_ = nullptr;
    QPushButton *rewardsRefreshButton_ = nullptr;

    QString activeEventId_;
    bool predictionLocked_ = false;

    pajlada::Signals::SignalHolder signalHolder_;
};

}  // namespace limerino
}  // namespace chatterino
