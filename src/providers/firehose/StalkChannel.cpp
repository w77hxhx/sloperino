// SPDX-FileCopyrightText: 2026 Contributors to Chatterino <https://chatterino.com>
//
// SPDX-License-Identifier: MIT

#include "providers/firehose/StalkChannel.hpp"

#include "Application.hpp"
#include "providers/firehose/FirehoseManager.hpp"

namespace chatterino {

StalkChannel::StalkChannel(const QString &targetUser)
    : Channel(QStringLiteral("/stalk/") +
                  (targetUser.startsWith('#')
                       ? targetUser.mid(1).trimmed().toLower()
                       : targetUser.trimmed().toLower()),
              Channel::Type::TwitchStalk)
    , targetUser_(targetUser.startsWith('#') ? targetUser.mid(1).trimmed()
                                             : targetUser.trimmed())
    , customDisplayName_(QStringLiteral("Stalk — #%1")
                             .arg(targetUser.startsWith('#')
                                      ? targetUser.mid(1).trimmed()
                                      : targetUser.trimmed()))
{
}

StalkChannel::~StalkChannel() = default;

const QString &StalkChannel::getDisplayName() const
{
    return this->customDisplayName_;
}

const QString &StalkChannel::getLocalizedName() const
{
    return this->customDisplayName_;
}

const QString &StalkChannel::targetUser() const
{
    return this->targetUser_;
}

bool StalkChannel::canReconnect() const
{
    return true;
}

void StalkChannel::reconnect()
{
    if (auto *app = tryGetApp())
    {
        if (auto *mgr = app->getFirehose())
        {
            mgr->reconnectAll();
        }
    }
}

}  // namespace chatterino
