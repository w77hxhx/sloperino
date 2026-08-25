// SPDX-License-Identifier: MIT

#include "providers/limerino/LimerinoRateLimiter.hpp"

#include "common/network/NetworkRequest.hpp"
#include "common/network/NetworkResult.hpp"

#include <QTimer>

#include <optional>

namespace chatterino {

LimerinoRateLimiter &LimerinoRateLimiter::instance()
{
    static LimerinoRateLimiter inst;
    return inst;
}

void LimerinoRateLimiter::execute(const QString &bucketKey,
                                  const std::function<NetworkRequest()> &makeRequest,
                                  const NetworkSuccessCallback &onSuccess,
                                  const NetworkErrorCallback &onError,
                                  int maxRetries)
{
    if (maxRetries < 0)
    {
        maxRetries = 0;
    }
    auto &bucket = this->buckets_[bucketKey];
    if (bucket.inFlight > 0)
    {
        // One in-flight per bucket: re-queued after a short spacing delay.
        QTimer::singleShot(MIN_SPACING_MS, this,
                           [this, bucketKey, makeRequest, onSuccess, onError,
                            maxRetries] {
                               this->execute(bucketKey, makeRequest, onSuccess,
                                             onError, maxRetries);
                           });
        return;
    }
    this->runOne(bucketKey, makeRequest, onSuccess, onError, 0, maxRetries);
}

void LimerinoRateLimiter::runOne(
    const QString &bucketKey,
    const std::function<NetworkRequest()> &makeRequest,
    const NetworkSuccessCallback &onSuccess, const NetworkErrorCallback &onError,
    int attempt, int maxRetries)
{
    auto &bucket = this->buckets_[bucketKey];
    ++bucket.inFlight;

    NetworkRequest request = makeRequest();
    std::move(request)
        .onSuccess([this, bucketKey,
                    onSuccess](NetworkResult result) {
            auto &b = this->buckets_[bucketKey];
            --b.inFlight;
            b.backoffMs = 1000;  // success resets the backoff ladder
            if (onSuccess)
            {
                onSuccess(std::move(result));
            }
        })
        .onError([this, bucketKey, makeRequest, onSuccess, onError, attempt,
                  maxRetries](NetworkResult result) {
            auto &b = this->buckets_[bucketKey];
            --b.inFlight;

            const bool retryable =
                result.status().has_value() && *result.status() == 429;
            if (retryable && attempt < maxRetries)
            {
                const int wait = b.backoffMs;
                b.backoffMs = qMin(b.backoffMs * 2, BACKOFF_CAP_MS);
                QTimer::singleShot(wait, this,
                                   [this, bucketKey, makeRequest, onSuccess,
                                    onError, attempt, maxRetries] {
                                       this->runOne(bucketKey, makeRequest,
                                                    onSuccess, onError,
                                                    attempt + 1, maxRetries);
                                   });
                return;
            }

            if (onError)
            {
                onError(std::move(result));
            }
        })
        .execute();
}

}  // namespace chatterino
