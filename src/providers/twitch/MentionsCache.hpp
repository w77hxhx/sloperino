// SPDX-FileCopyrightText: 2026 Contributors to Chatterino <https://chatterino.com>
//
// SPDX-License-Identifier: MIT

#pragma once

#include "common/Channel.hpp"

#include <pajlada/signals/signalholder.hpp>
#include <QDateTime>
#include <QSet>
#include <QString>
#include <QTimer>

#include <memory>
#include <vector>

namespace chatterino {

/// Persists the /mentions channel across restarts.
///
/// Mirrors the StalkChannel cache: every message appended to the mentions
/// channel is stored (debounced) into a JSON file under the cache directory
/// and re-inserted (in time order) on the next start.
class MentionsCache final
{
public:
    explicit MentionsCache(ChannelPtr channel);
    ~MentionsCache();

    MentionsCache(const MentionsCache &) = delete;
    MentionsCache &operator=(const MentionsCache &) = delete;

private:
    struct CachedItem {
        QString id;
        qint64 timeMs{0};
        QString channelName;
        QString loginName;
        QString displayName;
        QString text;
        QString color;
        bool highlighted{false};
    };

    void appendMessage(const MessagePtr &message);
    void loadCache();
    void saveCache();
    void scheduleSaveCache();

    QString getCacheFilePath() const;

    ChannelPtr channel_;
    std::vector<CachedItem> cachedItems_;
    QSet<QString> cachedIds_;
    std::unique_ptr<QTimer> saveTimer_;
    pajlada::Signals::SignalHolder holder_;
};

}  // namespace chatterino
