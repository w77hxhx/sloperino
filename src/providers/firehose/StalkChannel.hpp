// SPDX-FileCopyrightText: 2026 Contributors to Chatterino <https://chatterino.com>
//
// SPDX-License-Identifier: MIT

#pragma once

#include "common/Channel.hpp"

namespace chatterino {

class StalkChannel final : public Channel
{
public:
    explicit StalkChannel(const QString &targetUser);
    ~StalkChannel() override;

    const QString &getDisplayName() const override;
    const QString &getLocalizedName() const override;

    const QString &targetUser() const;

    bool canReconnect() const override;
    void reconnect() override;

private:
    QString targetUser_;
    QString customDisplayName_;
};

}  // namespace chatterino
