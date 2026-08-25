// SPDX-License-Identifier: MIT

#include "providers/limerino/autoactions/LimerinoAutoActionController.hpp"

#include "providers/limerino/autoactions/LimerinoAutoAction.hpp"
#include "providers/limerino/autoactions/LimerinoAutoActionStore.hpp"
#include "providers/limerino/autoactions/LimerinoAutoActionRuntime.hpp"
#include "singletons/Settings.hpp"

namespace chatterino::limerino {

LimerinoAutoActionController *LimerinoAutoActionController::instance_ = nullptr;

LimerinoAutoActionController *LimerinoAutoActionController::instance()
{
    return LimerinoAutoActionController::instance_;
}

LimerinoAutoActionController::LimerinoAutoActionController(Settings &settings,
                                                           QObject *parent)
    : QObject(parent)
    , settings_(settings)
{
    LimerinoAutoActionController::instance_ = this;

    this->rebuildListener_.setCB([this] {
        this->rebuild();
    });
    this->rebuildListener_.addSetting(this->settings_.limerinoAutoActions,
                                      /*autoInvoke=*/false);

    QObject::connect(this, &LimerinoAutoActionController::rulesChanged, this, [] {
        LimerinoAutoActionRuntime_resetCooldowns();
    });
}

void LimerinoAutoActionController::rebuild()
{
    {
        auto access = this->channelCache_.access();
        access->clear();
    }
    Q_EMIT rulesChanged();
}

LimerinoAutoActionController::Shared LimerinoAutoActionController::resolve(
    const QString &channelKey) const
{
    // Fast path: already computed for this channel key.
    {
        auto access = this->channelCache_.accessConst();
        const auto it = access->find(channelKey);
        if (it != access->end())
        {
            return it.value();
        }
    }

    // Slow path: gather every enabled rule whose scope matches this channel.
    const auto all = loadLimerinoAutoActions();

    auto resolved = std::make_shared<std::vector<LimerinoAutoAction>>();
    resolved->reserve(all.size());
    for (const auto &rule : all)
    {
        if (!rule.enabled)
        {
            continue;
        }
        if (!rule.matchesChannel(channelKey))
        {
            continue;
        }
        resolved->push_back(rule);
    }

    auto access = this->channelCache_.access();
    access->insert(channelKey, resolved);
    return resolved;
}

}  // namespace chatterino::limerino
