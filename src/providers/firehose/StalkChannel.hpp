// SPDX-FileCopyrightText: 2026 Contributors to Chatterino <https://chatterino.com>
//
// SPDX-License-Identifier: MIT

#pragma once

#include "common/Channel.hpp"

#include <QDateTime>
#include <QString>
#include <QTimer>

#include <cstdint>
#include <memory>
#include <vector>

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

    void addStalkMessage(const MessagePtr &msg, const QByteArray &rawPayload);

private:
    struct StalkCachedItem {
        int64_t timeMs{0};
        QString channel;
        QString username;
        QString displayName;
        QString text;
        QString raw;
    };

    void loadCache();
    void saveCache();
    void scheduleSaveCache();

    QString getCacheFilePath() const;

    QString targetUser_;
    QString customDisplayName_;

    std::vector<StalkCachedItem> cachedItems_;
    std::unique_ptr<QTimer> saveTimer_;
};

}  // namespace chatterino
