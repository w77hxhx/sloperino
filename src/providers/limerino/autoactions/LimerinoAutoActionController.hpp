// SPDX-License-Identifier: MIT
// Per-channel resolved auto-action rule cache (batch N5).
//
// One controller, owned by the application; watches the `/limerino/autoActions`
// setting and rebuilds a channel-keyed shared-pointer map so the hot path
// (N6's message-received evaluation) never walks the full rule list per
// message. Mirrors the HighlightController resolveChecks shape.

#pragma once

#include "common/UniqueAccess.hpp"

#include <QHash>
#include <QObject>
#include <QString>
#include <QUuid>

#include <memory>
#include <vector>

#include <pajlada/settings/settinglistener.hpp>
#include <pajlada/signals/signalholder.hpp>

namespace chatterino {

class Channel;
class Settings;

}  // namespace chatterino

namespace chatterino::limerino {

struct LimerinoAutoAction;

class LimerinoAutoActionController : public QObject
{
    Q_OBJECT

public:
    explicit LimerinoAutoActionController(Settings &settings,
                                          QObject *parent = nullptr);

    /// Process-wide access. Constructed by Settings (batch N5); null in tests
    /// unless one was installed. Mirrors patterns used by HighlightGroupController.
    static LimerinoAutoActionController *instance();

    /// Resolved rule set for the given channel key ("twitch:forsen",
    /// "kick:user", "special:whispers" ...). Cached per channel; invalidated
    /// whenever the setting changes.
    using Shared = std::shared_ptr<const std::vector<LimerinoAutoAction>>;
    [[nodiscard]] Shared resolve(const QString &channelKey) const;

    /// Drop every cached entry and emit rulesChanged. Called automatically
    /// whenever the setting changes; tests may call it manually.
    void rebuild();

Q_SIGNALS:
    /// Emitted once per rebuild, after all caches are cleared.
    void rulesChanged();

private:
    Settings &settings_;
    mutable UniqueAccess<QHash<QString, Shared>> channelCache_;
    pajlada::SettingListener rebuildListener_;
    pajlada::Signals::SignalHolder signalHolder_;

    static LimerinoAutoActionController *instance_;
};

}  // namespace chatterino::limerino
