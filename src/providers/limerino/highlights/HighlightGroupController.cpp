// SPDX-License-Identifier: MIT

#include "providers/limerino/highlights/HighlightGroupController.hpp"

#include "singletons/Settings.hpp"

namespace chatterino {

HighlightGroupController::HighlightGroupController(Settings &settings,
                                                   QObject *parent)
    : QObject(parent)
    , settings_(settings)
{
    this->ensureDefaultGroup();
}

void HighlightGroupController::ensureDefaultGroup()
{
    auto items = this->settings_.highlightGroups.readOnly();
    for (const auto &group : *items)
    {
        if (group.isDefault())
        {
            return;
        }
    }

    // Not found -- create Default as "everywhere".
    this->settings_.highlightGroups.append(
        HighlightGroup(HighlightGroup::DEFAULT_ID, QStringLiteral("Default"),
                       HighlightGroup::Scope::AllExcept, {}));
}

std::optional<HighlightGroup> HighlightGroupController::findGroup(
    const QUuid &id) const
{
    auto items = this->settings_.highlightGroups.readOnly();
    for (const auto &group : *items)
    {
        if (group.id() == id)
        {
            return group;
        }
    }
    return std::nullopt;
}

QList<QUuid> HighlightGroupController::matchingGroupIds(
    const QString &channelKey) const
{
    QList<QUuid> result;
    auto items = this->settings_.highlightGroups.readOnly();
    for (const auto &group : *items)
    {
        if (group.matches(channelKey))
        {
            result.append(group.id());
        }
    }
    return result;
}

}  // namespace chatterino
