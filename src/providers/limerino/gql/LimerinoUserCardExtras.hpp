// SPDX-License-Identifier: MIT
// Usercard "extra" fields fetched together in one combined inline GQL query:
//   preferredLanguageTag (user.settings), primaryTeam (a single team), and
//   the sub record (user.relationship(...).subscriptionBenefit + tenure).
// Source-of-truth shapes: gqlreference/gql/{fragments.js,user/queries.js}.
// One document per card open; per-field permission failures degrade to
// "field absent", never to blanking siblings.

#pragma once

#include <QJsonObject>
#include <QString>

#include <functional>
#include <optional>

namespace chatterino::LimerinoAuth::gql {

struct LimerinoSubDetail {
    QString platform;              // raw wire value; empty when field absent
    bool purchasedWithPrime = false;
    QString tier;                  // raw wire value (e.g. "1000"/"2000"/"3000")
    bool isGift = false;
    QString gifterDisplayName;     // displayName, else login; empty if none
    QString thirdPartySKU;         // raw wire value; empty when field absent
    int tenureMonths = 0;          // 0 when tenure is unknown / not shown
};

struct LimerinoUserCardExtras {
    QString preferredLanguageTag;    // empty when absent or settingsFailed
    QString primaryTeamName;         // empty when user is on no team (ruling 1)
    std::optional<LimerinoSubDetail> subscription;  // nullopt = none / absent

    // True when errors[] reported a failure on that field path. Distinguishes
    // "honestly empty" from "could not read" for UI (tooltip) and cache policy.
    bool settingsFailed = false;
    bool relationshipFailed = false;
    bool primaryTeamFailed = false;

    bool empty() const
    {
        return this->preferredLanguageTag.isEmpty() &&
               this->primaryTeamName.isEmpty() &&
               !this->subscription.has_value() && !this->settingsFailed;
    }

    bool hasFieldFailure() const
    {
        return this->settingsFailed || this->relationshipFailed ||
               this->primaryTeamFailed;
    }
};

// Parse one op-doc's data object. `settingsFailed` / `relationshipFailed`
// distinguish "field permission-denied / errored" from "honestly empty", both
// of which surface as null in some Twitch behaviours - either maps to absent.
// Never throws; any malformed input just yields the absent state.
LimerinoUserCardExtras parseUserCardExtras(const QJsonObject &userObj,
                                           bool settingsFailed,
                                           bool relationshipFailed);

// Fetch all three in one combined request, rate-limited and token-resolved.
// `cb` runs on the GUI thread (LimerinoRateLimiter guarantee). `cb(nullopt)`
// = total failure or resolver-abort (caller renders nothing); `cb(extras)`
// may be fully filled, partially filled (field-level null), or .empty() (user
// has no extras). Results are cached per-user for CACHE_TTL_MS unless a
// requested field failed (partial errors[] path) — those are displayed but
// not cached so the next open retries. Negative results are NOT cached.
void fetchUserCardExtras(
    const QString &targetUserId, const QString &channelId,
    std::function<void(std::optional<LimerinoUserCardExtras>)> cb);

}  // namespace chatterino::LimerinoAuth::gql
