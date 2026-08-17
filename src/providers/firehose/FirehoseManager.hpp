// SPDX-FileCopyrightText: 2026 Contributors to Chatterino <https://chatterino.com>
//
// SPDX-License-Identifier: MIT

#pragma once

#include "common/ChatterinoSetting.hpp"
#include "common/websockets/WebSocketPool.hpp"
#include "providers/firehose/FirehoseChannel.hpp"

#include <pajlada/signals/signalholder.hpp>
#include <QByteArray>
#include <QObject>
#include <QString>
#include <QTimer>
#include <QUrl>

#include <deque>
#include <memory>
#include <mutex>
#include <string>
#include <unordered_set>
#include <vector>

namespace chatterino {

class Channel;
using ChannelPtr = std::shared_ptr<Channel>;
class FirehoseWsListener;

class FirehoseManager final : public QObject
{
    Q_OBJECT

    friend class FirehoseWsListener;

public:
    FirehoseManager();
    ~FirehoseManager() override;

    std::shared_ptr<FirehoseChannel> getChannel() const;

    void initialize();
    void reconnectAll();

private:
    struct Endpoint {
        QString name;
        QUrl url;
        BoolSetting *enabledSetting{nullptr};
        WebSocketHandle handle;
        bool isConnected{false};
        int reconnectBackoffMs{2000};
        std::unique_ptr<QTimer> reconnectTimer;
    };

    void initEndpoints();
    void connectEndpoint(size_t index);
    void disconnectEndpoint(size_t index);
    void scheduleReconnect(size_t index);

    void onRawMessageReceived(QByteArray data);
    void processBatch();
    void updateStats();

    MessagePtr parseRawPayload(const QByteArray &data, QString &outMsgId);
    MessagePtr parseIrcLine(const QByteArray &data, QString &outMsgId);
    MessagePtr parseJsonPayload(const QByteArray &data, QString &outMsgId);

    WebSocketPool wsPool_{QStringLiteral("Firehose")};
    std::shared_ptr<FirehoseChannel> channel_;

    std::vector<Endpoint> endpoints_;

    // Thread-safe raw message queue
    std::mutex queueMutex_;
    std::vector<QByteArray> rawQueue_;

    // Deduplication ring-buffer cache
    std::unordered_set<std::string> seenIds_;
    std::deque<std::string> seenQueue_;
    static constexpr size_t MAX_DEDUP_CACHE_SIZE = 50000;

    // Timers
    QTimer batchTimer_;
    QTimer statsTimer_;

    // Statistics
    int messagesThisSecond_{0};
    int currentMsgPerSecond_{0};

    pajlada::Signals::SignalHolder signalHolder_;
};

}  // namespace chatterino
