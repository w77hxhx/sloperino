// SPDX-FileCopyrightText: 2026 Contributors to Chatterino <https://chatterino.com>
//
// SPDX-License-Identifier: MIT

#pragma once

#include "common/ChatterinoSetting.hpp"
#include "common/websockets/WebSocketPool.hpp"
#include "providers/firehose/FirehoseChannel.hpp"

#include <pajlada/signals/signalholder.hpp>
#include <QByteArray>
#include <QHash>
#include <QObject>
#include <QString>
#include <QTimer>
#include <QUrl>

#include <array>
#include <atomic>
#include <deque>
#include <memory>
#include <mutex>
#include <string>
#include <unordered_map>
#include <unordered_set>
#include <vector>

namespace chatterino {

class Channel;
using ChannelPtr = std::shared_ptr<Channel>;
class FirehoseWsListener;
class StalkChannel;

class FastDedupCache
{
public:
    static constexpr size_t NUM_SHARDS = 64;
    static constexpr size_t SHARD_CAPACITY = 2048;  // 64 * 2048 = 131,072 items

    FastDedupCache()
    {
        for (auto &shard : this->shards_)
        {
            shard.ring.resize(SHARD_CAPACITY, 0);
            shard.set.reserve(SHARD_CAPACITY);
        }
    }

    bool testAndSet(uint64_t hash)
    {
        if (hash == 0)
        {
            return true;
        }

        auto &shard = this->shards_[hash % NUM_SHARDS];
        std::lock_guard<std::mutex> lock(shard.mtx);

        if (shard.set.find(hash) != shard.set.end())
        {
            return false;  // Duplicate across endpoints
        }

        if (shard.count < SHARD_CAPACITY)
        {
            shard.ring[shard.count] = hash;
            shard.count++;
        }
        else
        {
            uint64_t old = shard.ring[shard.head];
            shard.set.erase(old);
            shard.ring[shard.head] = hash;
            shard.head = (shard.head + 1) % SHARD_CAPACITY;
        }

        shard.set.insert(hash);
        return true;  // New unique item
    }

    void clear()
    {
        for (auto &shard : this->shards_)
        {
            std::lock_guard<std::mutex> lock(shard.mtx);
            shard.set.clear();
            shard.head = 0;
            shard.count = 0;
        }
    }

private:
    struct Shard {
        std::mutex mtx;
        std::unordered_set<uint64_t> set;
        std::vector<uint64_t> ring;
        size_t head{0};
        size_t count{0};
    };

    std::array<Shard, NUM_SHARDS> shards_;
};

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

    void registerStalkChannel(const std::shared_ptr<StalkChannel> &channel);
    void unregisterStalkChannel(const std::shared_ptr<StalkChannel> &channel);

    void addFirehoseConsumer();
    void removeFirehoseConsumer();
    bool isNeeded() const;
    void checkConnectionState();

    enum class EndpointStatus {
        Disabled,
        Connecting,
        Connected,
        Reconnecting,
    };

    struct EndpointStatusInfo {
        QString name;
        QUrl url;
        EndpointStatus status;
        bool enabled;
    };

    // Returns a snapshot of connection status for each endpoint (UI use)
    QVector<EndpointStatusInfo> getEndpointStatuses() const;

private:
    struct Endpoint {
        QString name;
        QUrl url;
        BoolSetting *enabledSetting{nullptr};
        WebSocketHandle handle;
        bool isConnected{false};
        int reconnectBackoffMs{2000};
        std::unique_ptr<QTimer> reconnectTimer;
        // Generation counter: incremented on each disconnect so stale
        // WebSocketListener callbacks from the previous socket are ignored.
        std::atomic<uint32_t> epoch{0};
        EndpointStatus status{EndpointStatus::Disabled};
    };

    void initEndpoints();
    void connectEndpoint(size_t index);
    void disconnectEndpoint(size_t index);
    void scheduleReconnect(size_t index, uint32_t epoch);
    void onEndpointConnected(size_t index, uint32_t epoch);

    void onRawDataReceivedFromWorker(const char *ptr, size_t len);
    void processBatch();
    void updateStats();
    void runWatchdog();

    MessagePtr parseRawPayload(const QByteArray &data, QString &outMsgId);
    MessagePtr parseIrcLine(const QByteArray &data, QString &outMsgId);
    MessagePtr parseJsonPayload(const QByteArray &data, QString &outMsgId);

    WebSocketPool wsPool_{QStringLiteral("Firehose")};
    std::shared_ptr<FirehoseChannel> channel_;

    std::vector<Endpoint> endpoints_;

    // Ultra-fast sharded deduplication cache (zero allocations during streaming)
    FastDedupCache dedupCache_;

    // Thread-safe batch queue from worker threads to GUI processing
    std::mutex queueMutex_;
    std::vector<QByteArray> incomingQueue_;

    // Timers
    QTimer batchTimer_;
    QTimer statsTimer_;
    QTimer watchdogTimer_;

    // Consumer state
    std::atomic<int> firehoseAttachedCount_{0};
    bool isRunning_{false};

    // Atomic throughput statistics
    std::atomic<int> rawMessagesReceived_{0};
    int currentMsgPerSecond_{0};
    int secondsSinceLastSpeedCheck_{0};

    QHash<QString, std::shared_ptr<Channel>> fallbackChannels_;
    std::mutex stalkMutex_;
    std::vector<std::weak_ptr<StalkChannel>> stalkChannels_;

    pajlada::Signals::SignalHolder signalHolder_;
};

}  // namespace chatterino
