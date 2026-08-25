// SPDX-License-Identifier: MIT
// On-demand "pinned message" banner, mounted at the top of one specific chat
// split (inserted dynamically as a layout child, so it moves and resizes with
// the chat). Renders the pinned message with the real ChannelView pipeline -
// exactly like it looks in chat.

#pragma once

#include "messages/Message.hpp"
#include "widgets/BaseWidget.hpp"

#include <pajlada/signals/signalholder.hpp>
#include <QColor>
#include <QDateTime>

class QLabel;
class QPushButton;

namespace chatterino {
class ChannelView;
class Channel;
class Split;
using ChannelPtr = std::shared_ptr<Channel>;

namespace limerino {

class LimerinoPinView final : public BaseWidget
{
    Q_OBJECT

public:
    // Mounts itself into `split`'s layout (right above the chat view).
    explicit LimerinoPinView(Split *split);

    void setMessage(const MessagePtr &message, const QString &pinnedByName,
                    const QDateTime &pinnedAt);
    // Used when the message is no longer in channel history:
    void setPlainMessage(const QString &senderName, const QString &text,
                         const QString &pinnedByName,
                         const QDateTime &pinnedAt);
    void dismiss();

private:
    void refreshInfoLabel();
    void refreshTint();
    void paintEvent(QPaintEvent *event) override;

    Split *split_ = nullptr;
    ChannelPtr virtualChannel_;
    ChannelView *view_ = nullptr;
    QLabel *infoLabel_ = nullptr;
    QPushButton *closeButton_ = nullptr;

    QString pinnedByName_;
    QDateTime pinnedAt_;
    QColor tint_;

    pajlada::Signals::SignalHolder signalHolder_;
};

}  // namespace limerino
}  // namespace chatterino
