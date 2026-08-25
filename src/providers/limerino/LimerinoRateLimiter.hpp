// SPDX-License-Identifier: MIT
// Shared request queue: per-host budget, 429 backoff.
// NOTE: NetworkResult does not expose response headers in this tree, so the
// standard `Retry-After` header is unreadable here; backoff is our own
// (1, 2, 4, 8, ... capped at 30 s), documented rather than invented.

#pragma once

#include <QObject>
#include <QString>

#include "common/network/NetworkCommon.hpp"

#include <QHash>

#include <functional>

namespace chatterino {

class NetworkRequest;

class LimerinoRateLimiter final : public QObject
{
    Q_OBJECT

public:
    static LimerinoRateLimiter &instance();

    // makeRequest is a factory (NetworkRequest is move-only; retries need a
    // fresh instance). Callbacks run on the GUI thread.
    void execute(const QString &bucketKey,
                 const std::function<NetworkRequest()> &makeRequest,
                 const NetworkSuccessCallback &onSuccess,
                 const NetworkErrorCallback &onError, int maxRetries = 4);

private:
    struct Bucket {
        int inFlight = 0;
        int backoffMs = 1000;
    };

    static constexpr int MIN_SPACING_MS = 400;
    static constexpr int BACKOFF_CAP_MS = 30000;

    void pump(const QString &bucketKey);
    void runOne(const QString &bucketKey,
                const std::function<NetworkRequest()> &makeRequest,
                const NetworkSuccessCallback &onSuccess,
                const NetworkErrorCallback &onError, int attempt, int maxRetries);

    QHash<QString, Bucket> buckets_;
};

}  // namespace chatterino
