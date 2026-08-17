// SPDX-FileCopyrightText: 2026 Contributors to Chatterino <https://chatterino.com>
//
// SPDX-License-Identifier: MIT

#include "providers/firehose/FirehoseChannel.hpp"

#include "Application.hpp"
#include "providers/firehose/FirehoseManager.hpp"
#include "singletons/Settings.hpp"

namespace chatterino {

FirehoseChannel::FirehoseChannel()
    : Channel(QStringLiteral("/firehose"), Channel::Type::TwitchFirehose)
{
    this->messages_.setLimit(
        static_cast<size_t>(getSettings()->firehoseMaxMessages.getValue()));

    getSettings()->firehoseMaxMessages.connect(
        [this](int limit) {
            this->messages_.setLimit(
                static_cast<size_t>(std::max(1000, limit)));
        },
        false);
}

const QString &FirehoseChannel::getDisplayName() const
{
    return this->customDisplayName_;
}

const QString &FirehoseChannel::getLocalizedName() const
{
    return this->customDisplayName_;
}

void FirehoseChannel::updateStatus(int msgPerSecond, int activeSockets,
                                   int totalSockets)
{
    QString newName;
    if (getSettings()->firehoseShowRateInTitle.getValue())
    {
        newName = QStringLiteral("Firehose (%1 msg/s • %2/%3)")
                      .arg(QString::number(msgPerSecond),
                           QString::number(activeSockets),
                           QString::number(totalSockets));
    }
    else
    {
        newName = QStringLiteral("Firehose");
    }

    if (this->customDisplayName_ != newName)
    {
        this->customDisplayName_ = newName;
        this->displayNameChanged.invoke();
    }
}

bool FirehoseChannel::canReconnect() const
{
    return true;
}

void FirehoseChannel::reconnect()
{
    if (auto *app = tryGetApp())
    {
        if (auto *mgr = app->getFirehose())
        {
            mgr->reconnectAll();
        }
    }
}

void FirehoseChannel::addMessagesBatch(const std::vector<MessagePtr> &messages)
{
    if (messages.empty())
    {
        return;
    }

    for (const auto &msg : messages)
    {
        this->addMessage(msg, MessageContext::Original);
    }
}

}  // namespace chatterino
