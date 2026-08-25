// SPDX-License-Identifier: MIT
// Owns the highlightGroups setting and guarantees the well-known Default group
// always exists. H1 scope: data model + bootstrap only. The per-channel
// resolved-check cache lands in H2.

#pragma once

#include "providers/limerino/highlights/HighlightGroup.hpp"

#include <pajlada/signals/signalholder.hpp>
#include <QObject>
#include <QUuid>

#include <optional>

namespace chatterino {

class Settings;

class HighlightGroupController : public QObject
{
public:
    explicit HighlightGroupController(Settings &settings,
                                      QObject *parent = nullptr);

    /// Ensures a group with HighlightGroup::DEFAULT_ID exists in the settings
    /// vector. If absent, creates it as AllExcept with an empty channel list
    /// (i.e. "everywhere"). Called once during construction.
    void ensureDefaultGroup();

    /// Look up a group by ID. Returns std::nullopt if not found.
    std::optional<HighlightGroup> findGroup(const QUuid &id) const;

    /// The ordered list of group IDs that match the given channel key.
    QList<QUuid> matchingGroupIds(const QString &channelKey) const;

private:
    Settings &settings_;
};

}  // namespace chatterino
