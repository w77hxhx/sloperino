// SPDX-License-Identifier: MIT
// Persistence for auto-action rules (batch N5). Lives on top of the
// `/limerino/autoActions` QStringSetting; the whole list serialises as a JSON
// array, one object per rule. This module is the single point of contact
// with the raw setting â€” everything else goes through it.

#pragma once

#include <QUuid>
#include <QVector>

namespace chatterino::limerino {

struct LimerinoAutoAction;

/// All rules, in stored order. Malformed entries are dropped.
QVector<LimerinoAutoAction> loadLimerinoAutoActions();
void saveLimerinoAutoActions(const QVector<LimerinoAutoAction> &rules);

/// Convenience mutators (used by the N7 editor). All persist immediately.
void addLimerinoAutoAction(LimerinoAutoAction rule);
void updateLimerinoAutoAction(const LimerinoAutoAction &rule);  // matches on id
void removeLimerinoAutoAction(const QUuid &id);

}  // namespace chatterino::limerino
