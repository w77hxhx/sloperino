// SPDX-FileCopyrightText: 2026 Contributors to Chatterino <https://chatterino.com>
//
// SPDX-License-Identifier: MIT

#pragma once

#include "common/Channel.hpp"

namespace chatterino {

class FirehoseChannel final : public Channel
{
public:
    FirehoseChannel();

    const QString &getDisplayName() const override;
    const QString &getLocalizedName() const override;

    void updateStatus(int msgPerSecond, int activeSockets, int totalSockets);

    bool canReconnect() const override;
    void reconnect() override;

    void addMessagesBatch(const std::vector<MessagePtr> &messages);

private:
    QString customDisplayName_{QStringLiteral("Firehose")};
};

}  // namespace chatterino
