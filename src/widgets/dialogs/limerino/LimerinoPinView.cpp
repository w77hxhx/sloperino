// SPDX-License-Identifier: MIT

#include "widgets/dialogs/limerino/LimerinoPinView.hpp"

#include "common/Channel.hpp"
#include "messages/Message.hpp"
#include "providers/twitch/TwitchChannel.hpp"
#include "singletons/Settings.hpp"
#include "widgets/helper/ChannelView.hpp"
#include "widgets/splits/Split.hpp"

#include <QHBoxLayout>
#include <QLabel>
#include <QPainter>
#include <QPushButton>
#include <QVBoxLayout>

namespace chatterino::limerino {

LimerinoPinView::LimerinoPinView(Split *split)
    : BaseWidget(split)
    , split_(split)
    , virtualChannel_(new Channel("", Channel::Type::None))
{
    auto *root = new QVBoxLayout(this);
    root->setContentsMargins(6, 4, 6, 4);
    root->setSpacing(2);

    auto *topRow = new QHBoxLayout;
    this->infoLabel_ = new QLabel(this);
    topRow->addWidget(this->infoLabel_, 1);

    this->closeButton_ = new QPushButton(QStringLiteral("x"), this);
    this->closeButton_->setFixedWidth(28);
    this->closeButton_->setToolTip(QStringLiteral("Dismiss pin view"));
    topRow->addWidget(this->closeButton_);
    root->addLayout(topRow);

    this->view_ = new ChannelView(this, split, ChannelView::Context::None);
    this->view_->setMaximumHeight(140);
    root->addWidget(this->view_);

    this->setVisible(true);
    this->refreshTint();

    QObject::connect(this->closeButton_, &QPushButton::clicked, this,
                     [this] { this->dismiss(); });

    // Tint follows the global highlight color (changeable under
    // Settings > Highlights), re-rendered live when the setting changes.
    getSettings()->highlightColor.connect(
        [this](const QString & /*value*/) { this->refreshTint(); },
        this->signalHolder_);

    // Mount dynamically into the split's layout: the widget goes directly
    // above the chat view and below any banners, so it moves and resizes
    // with the chat. index 2 == after header + existing pinned banner.
    if (auto *box = qobject_cast<QVBoxLayout *>(this->split_->layout()))
    {
        box->insertWidget(2, this);
    }
    else
    {
        this->setVisible(false);
    }
}

void LimerinoPinView::refreshTint()
{
    const QString text = getSettings()->highlightColor.getValue();
    const QColor color(text);
    this->tint_ = text.isEmpty() ? QColor() : color;
    this->update();
}

void LimerinoPinView::refreshInfoLabel()
{
    QString info = QStringLiteral("\U0001F4CC Pinned by %1")
                       .arg(this->pinnedByName_.isEmpty()
                                ? QStringLiteral("unknown mod")
                                : this->pinnedByName_);
    if (this->pinnedAt_.isValid())
    {
        info += QStringLiteral(", %1").arg(this->pinnedAt_.toLocalTime()
                                               .toString(QStringLiteral(
                                                   "yyyy-MM-dd hh:mm")));
    }
    this->infoLabel_->setText(info);
}

void LimerinoPinView::setMessage(const MessagePtr &message,
                                 const QString &pinnedByName,
                                 const QDateTime &pinnedAt)
{
    this->pinnedByName_ = pinnedByName;
    this->pinnedAt_ = pinnedAt;
    this->refreshInfoLabel();

    this->view_->setSourceChannel(this->split_->getChannel());
    this->virtualChannel_->addMessage(message, MessageContext::Repost);
    this->view_->setChannel(this->virtualChannel_);
}

void LimerinoPinView::setPlainMessage(const QString &senderName,
                                      const QString &text,
                                      const QString &pinnedByName,
                                      const QDateTime &pinnedAt)
{
    this->pinnedByName_ = pinnedByName;
    this->pinnedAt_ = pinnedAt;
    this->refreshInfoLabel();

    // Fallback: the message is out of local history, so only the plain text
    // payload from the GQL op is available (no badges/emote rendering).
    this->view_->setVisible(false);
    this->infoLabel_->setText(
        this->infoLabel_->text() +
        QStringLiteral(" - ") + senderName + QStringLiteral(": ") + text);
}

void LimerinoPinView::dismiss()
{
    this->setVisible(false);
    this->deleteLater();
}

void LimerinoPinView::paintEvent(QPaintEvent *event)
{
    if (this->tint_.isValid())
    {
        QPainter painter(this);
        painter.fillRect(this->rect(), this->tint_);
    }
    BaseWidget::paintEvent(event);
}

}  // namespace chatterino::limerino
