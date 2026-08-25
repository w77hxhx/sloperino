// SPDX-FileCopyrightText: 2018 Contributors to Chatterino <https://chatterino.com>
//
// SPDX-License-Identifier: MIT

#include "controllers/highlights/HighlightController.hpp"

#include "Application.hpp"
#include "common/QLogging.hpp"
#include "controllers/accounts/AccountController.hpp"
#include "controllers/highlights/HighlightBadge.hpp"
#include "controllers/highlights/HighlightCheck.hpp"
#include "controllers/highlights/HighlightPhrase.hpp"
#include "controllers/highlights/HighlightResult.hpp"
#include "messages/Message.hpp"
#include "messages/MessageBuilder.hpp"
#include "providers/colors/ColorProvider.hpp"
#include "providers/kick/KickAccount.hpp"
#include "providers/twitch/TwitchAccount.hpp"  // IWYU pragma: keep
#include "providers/twitch/TwitchBadge.hpp"
#include "singletons/Settings.hpp"

namespace {

using namespace chatterino;

auto highlightPhraseCheck(const HighlightPhrase &highlight) -> HighlightCheck
{
    return HighlightCheck{
        [highlight](const auto &args, const auto &twitchBadges,
                    const auto &senderName, const auto &originalMessage,
                    const auto &flags,
                    const auto self) -> std::optional<HighlightResult> {
            (void)args;
            (void)twitchBadges;
            (void)senderName;
            (void)flags;

            if (self)
            {
                return std::nullopt;
            }

            if (!highlight.isMatch(originalMessage))
            {
                return std::nullopt;
            }

            std::optional<QUrl> highlightSoundUrl;
            if (highlight.hasCustomSound())
            {
                highlightSoundUrl = highlight.getSoundUrl();
            }

            return HighlightResult{
                highlight.hasAlert(),       highlight.hasSound(),
                highlightSoundUrl,          highlight.getColor(),
                highlight.showInMentions(), {highlight.getPattern()},
            };
        }};
}

/// Wrap a global check with a null groupId (meaning: always applies).
auto globalCheck(HighlightCheck check) -> GroupedHighlightCheck
{
    return GroupedHighlightCheck{std::move(check), QUuid()};
}

void rebuildSubscriptionHighlights(Settings &settings,
                                   std::vector<GroupedHighlightCheck> &checks)
{
    if (settings.enableSubHighlight)
    {
        auto highlightSound = settings.enableSubHighlightSound.getValue();
        auto highlightAlert = settings.enableSubHighlightTaskbar.getValue();
        auto highlightSoundUrlValue = settings.subHighlightSoundUrl.getValue();
        std::optional<QUrl> highlightSoundUrl;
        if (!highlightSoundUrlValue.isEmpty())
        {
            highlightSoundUrl = highlightSoundUrlValue;
        }

        checks.emplace_back(globalCheck(HighlightCheck{
            [=](const auto &args, const auto &twitchBadges,
                const auto &senderName, const auto &originalMessage,
                const auto &flags,
                const auto self) -> std::optional<HighlightResult> {
                (void)twitchBadges;
                (void)senderName;
                (void)originalMessage;
                (void)flags;
                (void)self;

                if (!args.isSubscriptionMessage)
                {
                    return std::nullopt;
                }

                auto highlightColor =
                    ColorProvider::instance().color(ColorType::Subscription);

                return HighlightResult{
                    highlightAlert, highlightSound, highlightSoundUrl,
                    highlightColor, false,
                };
            }}));
    }
}

void rebuildFollowHighlights(Settings &settings,
                             std::vector<GroupedHighlightCheck> &checks)
{
    if (settings.enableFollowHighlight)
    {
        auto highlightSound = settings.enableFollowHighlightSound.getValue();
        auto highlightAlert = settings.enableFollowHighlightTaskbar.getValue();
        auto highlightSoundUrlValue =
            settings.followHighlightSoundUrl.getValue();
        std::optional<QUrl> highlightSoundUrl;
        if (!highlightSoundUrlValue.isEmpty())
        {
            highlightSoundUrl = highlightSoundUrlValue;
        }

        checks.emplace_back(globalCheck(HighlightCheck{
            [=](const auto &args, const auto &twitchBadges,
                const auto &senderName, const auto &originalMessage,
                const auto &flags,
                const auto self) -> std::optional<HighlightResult> {
                (void)args;
                (void)twitchBadges;
                (void)senderName;
                (void)originalMessage;
                (void)self;

                if (!flags.has(MessageFlag::Follow))
                {
                    return std::nullopt;
                }

                auto highlightColor =
                    ColorProvider::instance().color(ColorType::Follow);

                return HighlightResult{
                    highlightAlert, highlightSound, highlightSoundUrl,
                    highlightColor, false,
                };
            }}));
    }
}

void rebuildWhisperHighlights(Settings &settings,
                              std::vector<GroupedHighlightCheck> &checks)
{
    if (settings.enableWhisperHighlight)
    {
        auto highlightSound = settings.enableWhisperHighlightSound.getValue();
        auto highlightAlert = settings.enableWhisperHighlightTaskbar.getValue();
        auto highlightSoundUrlValue =
            settings.whisperHighlightSoundUrl.getValue();
        std::optional<QUrl> highlightSoundUrl;
        if (!highlightSoundUrlValue.isEmpty())
        {
            highlightSoundUrl = highlightSoundUrlValue;
        }

        checks.emplace_back(globalCheck(HighlightCheck{
            [=](const auto &args, const auto &twitchBadges,
                const auto &senderName, const auto &originalMessage,
                const auto &flags,
                const auto self) -> std::optional<HighlightResult> {
                (void)twitchBadges;
                (void)senderName;
                (void)originalMessage;
                (void)flags;
                (void)self;

                if (!args.isReceivedWhisper)
                {
                    return std::nullopt;
                }

                return HighlightResult{
                    highlightAlert,
                    highlightSound,
                    highlightSoundUrl,
                    ColorProvider::instance().color(ColorType::Whisper),
                    false,
                };
            }}));
    }
}

void rebuildReplyThreadHighlight(Settings &settings,
                                 std::vector<GroupedHighlightCheck> &checks)
{
    if (settings.enableThreadHighlight)
    {
        auto highlightSound = settings.enableThreadHighlightSound.getValue();
        auto highlightAlert = settings.enableThreadHighlightTaskbar.getValue();
        auto highlightSoundUrlValue =
            settings.threadHighlightSoundUrl.getValue();
        std::optional<QUrl> highlightSoundUrl;
        if (!highlightSoundUrlValue.isEmpty())
        {
            highlightSoundUrl = highlightSoundUrlValue;
        }
        auto highlightInMentions =
            settings.showThreadHighlightInMentions.getValue();
        checks.emplace_back(globalCheck(HighlightCheck{
            [=](const auto &, const auto &, const auto &, const auto &,
                const auto &flags,
                const auto self) -> std::optional<HighlightResult> {
                if (flags.has(MessageFlag::SubscribedThread) && !self)
                {
                    return HighlightResult{
                        highlightAlert,
                        highlightSound,
                        highlightSoundUrl,
                        ColorProvider::instance().color(
                            ColorType::ThreadMessageHighlight),
                        highlightInMentions,
                    };
                }

                return std::nullopt;
            }}));
    }
}

void rebuildMessageHighlights(Settings &settings,
                              std::vector<GroupedHighlightCheck> &checks)
{
    auto currentUser = getApp()->getAccounts()->twitch.getCurrent();
    QString currentUsername = currentUser->getUserName();

    if (settings.enableSelfHighlight && !currentUsername.isEmpty() &&
        !currentUser->isAnon())
    {
        HighlightPhrase highlight(
            currentUsername, settings.showSelfHighlightInMentions,
            settings.enableSelfHighlightTaskbar,
            settings.enableSelfHighlightSound, false, false,
            settings.selfHighlightSoundUrl.getValue(),
            ColorProvider::instance().color(ColorType::SelfHighlight));

        checks.emplace_back(globalCheck(highlightPhraseCheck(highlight)));
    }

    auto kickUser = getApp()->getAccounts()->kick.current();
    auto kickUsername = kickUser->username();
    if (settings.enableSelfHighlight && !kickUsername.isEmpty() &&
        !kickUser->isAnonymous())
    {
        HighlightPhrase highlight(
            kickUsername, settings.showSelfHighlightInMentions,
            settings.enableSelfHighlightTaskbar,
            settings.enableSelfHighlightSound, false, false,
            settings.selfHighlightSoundUrl.getValue(),
            ColorProvider::instance().color(ColorType::SelfHighlight));

        checks.emplace_back(globalCheck(highlightPhraseCheck(highlight)));
    }

    auto messageHighlights = settings.highlightedMessages.readOnly();
    for (const auto &highlight : *messageHighlights)
    {
        checks.emplace_back(
            // groupable: record which group produced this check
            GroupedHighlightCheck{highlightPhraseCheck(highlight),
                                  highlight.groupId()});
    }

    if (settings.enableAutomodHighlight)
    {
        const auto highlightSound =
            settings.enableAutomodHighlightSound.getValue();
        const auto highlightAlert =
            settings.enableAutomodHighlightTaskbar.getValue();
        const auto highlightSoundUrlValue =
            settings.automodHighlightSoundUrl.getValue();
        auto highlightColor =
            ColorProvider::instance().color(ColorType::AutomodHighlight);

        checks.emplace_back(globalCheck(HighlightCheck{
            [=](const auto &, const auto &, const auto &,
                const auto &, const auto &flags,
                const auto) -> std::optional<HighlightResult> {
                if (!flags.has(MessageFlag::AutoModOffendingMessage))
                {
                    return std::nullopt;
                }

                std::optional<QUrl> highlightSoundUrl;
                if (!highlightSoundUrlValue.isEmpty())
                {
                    highlightSoundUrl = highlightSoundUrlValue;
                }

                return HighlightResult{
                    highlightAlert, highlightSound, highlightSoundUrl,
                    highlightColor, false,
                };
            }}));
    }
}

void rebuildUserHighlights(Settings &settings,
                           std::vector<GroupedHighlightCheck> &checks)
{
    auto userHighlights = settings.highlightedUsers.readOnly();

    if (settings.enableSelfMessageHighlight)
    {
        bool showInMentions = settings.showSelfMessageHighlightInMentions;

        checks.emplace_back(globalCheck(HighlightCheck{
            [showInMentions](
                const auto &args, const auto &twitchBadges,
                const auto &senderName, const auto &originalMessage,
                const auto &flags,
                const auto self) -> std::optional<HighlightResult> {
                (void)args;
                (void)twitchBadges;
                (void)senderName;
                (void)flags;
                (void)originalMessage;

                if (!self)
                {
                    return std::nullopt;
                }

                auto highlightColor = ColorProvider::instance().color(
                    ColorType::SelfMessageHighlight);

                return HighlightResult{false, false, (QUrl) nullptr,
                                       highlightColor, showInMentions};
            }}));
    }

    for (const auto &highlight : *userHighlights)
    {
        auto check = HighlightCheck{
            [highlight](const auto &args, const auto &twitchBadges,
                        const auto &senderName, const auto &originalMessage,
                        const auto &flags,
                        const auto self) -> std::optional<HighlightResult> {
                (void)args;
                (void)twitchBadges;
                (void)originalMessage;
                (void)flags;
                (void)self;

                if (!highlight.isMatch(senderName))
                {
                    return std::nullopt;
                }

                std::optional<QUrl> highlightSoundUrl;
                if (highlight.hasCustomSound())
                {
                    highlightSoundUrl = highlight.getSoundUrl();
                }

                return HighlightResult{
                    highlight.hasAlert(),       highlight.hasSound(),
                    highlightSoundUrl,          highlight.getColor(),
                    highlight.showInMentions(),
                };
            }};
        // groupable: record which group produced this check
        checks.emplace_back(
            GroupedHighlightCheck{std::move(check), highlight.groupId()});
    }
}

void rebuildBadgeHighlights(Settings &settings,
                            std::vector<GroupedHighlightCheck> &checks)
{
    auto badgeHighlights = settings.highlightedBadges.readOnly();

    for (const auto &highlight : *badgeHighlights)
    {
        auto check = HighlightCheck{
            [highlight](const auto &args, const auto &twitchBadges,
                        const auto &senderName, const auto &originalMessage,
                        const auto &flags,
                        const auto self) -> std::optional<HighlightResult> {
                (void)args;
                (void)senderName;
                (void)originalMessage;
                (void)flags;
                (void)self;

                for (const TwitchBadge &badge : twitchBadges)
                {
                    if (highlight.isMatch(badge))
                    {
                        std::optional<QUrl> highlightSoundUrl;
                        if (highlight.hasCustomSound())
                        {
                            highlightSoundUrl = highlight.getSoundUrl();
                        }

                        return HighlightResult{
                            highlight.hasAlert(),       highlight.hasSound(),
                            highlightSoundUrl,          highlight.getColor(),
                            highlight.showInMentions(),
                        };
                    }
                }

                return std::nullopt;
            }};
        // groupable: record which group produced this check
        checks.emplace_back(
            GroupedHighlightCheck{std::move(check), highlight.groupId()});
    }
}

}  // namespace

namespace chatterino {

HighlightController::HighlightController(Settings &settings,
                                         AccountController *accounts)
{
    assert(accounts != nullptr);

    this->rebuildListener_.addSetting(settings.enableSelfHighlight);
    this->rebuildListener_.addSetting(settings.enableSelfHighlightSound);
    this->rebuildListener_.addSetting(settings.enableSelfHighlightTaskbar);
    this->rebuildListener_.addSetting(settings.selfHighlightSoundUrl);
    this->rebuildListener_.addSetting(settings.showSelfHighlightInMentions);

    this->rebuildListener_.addSetting(settings.enableWhisperHighlight);
    this->rebuildListener_.addSetting(settings.enableWhisperHighlightSound);
    this->rebuildListener_.addSetting(settings.enableWhisperHighlightTaskbar);
    this->rebuildListener_.addSetting(settings.whisperHighlightSoundUrl);

    this->rebuildListener_.addSetting(settings.enableSubHighlight);
    this->rebuildListener_.addSetting(settings.enableSubHighlightSound);
    this->rebuildListener_.addSetting(settings.enableSubHighlightTaskbar);
    this->rebuildListener_.addSetting(settings.enableSelfMessageHighlight);
    this->rebuildListener_.addSetting(
        settings.showSelfMessageHighlightInMentions);

    this->rebuildListener_.addSetting(settings.subHighlightSoundUrl);

    this->rebuildListener_.addSetting(settings.enableFollowHighlight);
    this->rebuildListener_.addSetting(settings.enableFollowHighlightSound);
    this->rebuildListener_.addSetting(settings.enableFollowHighlightTaskbar);
    this->rebuildListener_.addSetting(settings.followHighlightSoundUrl);

    this->rebuildListener_.addSetting(settings.enableThreadHighlight);
    this->rebuildListener_.addSetting(settings.enableThreadHighlightSound);
    this->rebuildListener_.addSetting(settings.enableThreadHighlightTaskbar);
    this->rebuildListener_.addSetting(settings.threadHighlightSoundUrl);
    this->rebuildListener_.addSetting(settings.showThreadHighlightInMentions);

    this->rebuildListener_.addSetting(settings.enableAutomodHighlight);
    this->rebuildListener_.addSetting(settings.showAutomodInMentions);
    this->rebuildListener_.addSetting(settings.enableAutomodHighlightSound);
    this->rebuildListener_.addSetting(settings.enableAutomodHighlightTaskbar);
    this->rebuildListener_.addSetting(settings.automodHighlightSoundUrl);

    this->rebuildListener_.setCB([this, &settings] {
        qCDebug(chatterinoHighlights)
            << "Rebuild checks because a setting changed";
        this->rebuildChecks(settings);
    });

    this->signalHolder_.managedConnect(
        getSettings()->highlightedBadges.delayedItemsChanged,
        [this, &settings] {
            qCDebug(chatterinoHighlights)
                << "Rebuild checks because highlight badges changed";
            this->rebuildChecks(settings);
        });

    this->signalHolder_.managedConnect(
        getSettings()->highlightedUsers.delayedItemsChanged, [this, &settings] {
            qCDebug(chatterinoHighlights)
                << "Rebuild checks because highlight users changed";
            this->rebuildChecks(settings);
        });

    this->signalHolder_.managedConnect(
        getSettings()->highlightedMessages.delayedItemsChanged,
        [this, &settings] {
            qCDebug(chatterinoHighlights)
                << "Rebuild checks because highlight messages changed";
            this->rebuildChecks(settings);
        });

    this->signalHolder_.managedConnect(
        getSettings()->highlightGroups.delayedItemsChanged,
        [this, &settings] {
            qCDebug(chatterinoHighlights)
                << "Rebuild checks because highlight groups changed";
            this->rebuildChecks(settings);
        });

    this->bConnections.emplace_back(
        accounts->twitch.currentUserChanged.connect([this, &settings] {
            qCDebug(chatterinoHighlights)
                << "Rebuild checks because user swapped accounts";
            this->rebuildChecks(settings);
        }));

    this->signalHolder_.managedConnect(
        accounts->twitch.currentUserNameChanged, [this, &settings] {
            qCDebug(chatterinoHighlights)
                << "Rebuild checks because user name changed";
            this->rebuildChecks(settings);
        });

    this->signalHolder_.managedConnect(
        accounts->kick.currentUserChanged, [this, &settings] {
            qCDebug(chatterinoHighlights)
                << "Rebuild checks because Kick user changed";
            this->rebuildChecks(settings);
        });

    this->rebuildChecks(settings);
}

void HighlightController::rebuildChecks(Settings &settings)
{
    auto checks = this->checks_.access();
    checks->clear();

    rebuildSubscriptionHighlights(settings, *checks);

    rebuildFollowHighlights(settings, *checks);

    rebuildWhisperHighlights(settings, *checks);

    rebuildMessageHighlights(settings, *checks);

    rebuildUserHighlights(settings, *checks);

    rebuildReplyThreadHighlight(settings, *checks);

    rebuildBadgeHighlights(settings, *checks);

    // Invalidate both resolution caches -- they will be repopulated lazily
    // on the next check() call for each channel.
    auto chanAccess = this->channelChecks_.access();
    chanAccess->clear();
    auto setAccess = this->byGroupSet_.access();
    setAccess->clear();
}

/// Produce a sorted, canonical key for a set of group IDs so that identical
/// sets map to the same resolved-cache slot regardless of member order.
static QString groupSetKey(const std::vector<QUuid> &groupIds)
{
    QStringList strs;
    strs.reserve(groupIds.size());
    for (const auto &id : groupIds)
    {
        strs.append(id.toString(QUuid::WithoutBraces));
    }
    strs.sort();
    return strs.join(QStringLiteral(";"));
}

HighlightController::SharedCheckVector HighlightController::resolveChecks(
    Settings &settings, const QString &channelKey) const
{
    // Fast path 1: channel key already resolved.
    {
        auto chanAccess = this->channelChecks_.accessConst();
        auto it = chanAccess->find(channelKey);
        if (it != chanAccess->end())
        {
            return it.value();
        }
    }

    // Compute the set of group IDs whose scope matches this channel.
    std::vector<QUuid> matchingGroups;
    auto matching = settings.highlightGroups.readOnly();
    for (const auto &group : *matching)
    {
        if (group.matches(channelKey))
        {
            matchingGroups.push_back(group.id());
        }
    }

    const auto setKey = groupSetKey(matchingGroups);

    // Fast path 2: a different channel already produced this group set.
    {
        auto setAccess = this->byGroupSet_.accessConst();
        auto it = setAccess->find(setKey);
        if (it != setAccess->end())
        {
            return it.value();
        }
    }

    // Slow path: build the filtered vector, preserving the existing order
    // exactly (Subscription -> Follow -> Whisper -> Message -> User ->
    // Thread -> Badge).
    auto resolved = std::make_shared<std::vector<HighlightCheck>>();
    {
        auto masterAccess = this->checks_.accessConst();
        resolved->reserve(masterAccess->size());
        for (const auto &grouped : *masterAccess)
        {
            if (grouped.groupId.isNull())
            {
                // Global check: always included.
                resolved->push_back(grouped.check);
                continue;
            }
            if (std::find(matchingGroups.begin(), matchingGroups.end(),
                          grouped.groupId) != matchingGroups.end())
            {
                resolved->push_back(grouped.check);
            }
        }
    }

    // Store into both caches.
    {
        auto setAccess = this->byGroupSet_.access();
        setAccess->insert(setKey, resolved);
    }
    {
        auto chanAccess = this->channelChecks_.access();
        chanAccess->insert(channelKey, resolved);
    }

    return resolved;
}

std::pair<bool, HighlightResult> HighlightController::runChecks(
    const MessageParseArgs &args, const std::vector<TwitchBadge> &twitchBadges,
    const QString &senderName, const QString &originalMessage,
    const MessageFlags &messageFlags, MessagePlatform platform,
    const SharedCheckVector &checks) const
{
    bool highlighted = false;
    auto result = HighlightResult::emptyResult();

    bool self = false;
    switch (platform)
    {
        case MessagePlatform::AnyOrTwitch: {
            auto currentUser = getApp()->getAccounts()->twitch.getCurrent();
            self = senderName == currentUser->getUserName();
        }
        break;
        case MessagePlatform::Kick: {
            auto kickUser = getApp()->getAccounts()->kick.current();
            self =
                !kickUser->isAnonymous() && senderName == kickUser->username();
        }
        break;
        case MessagePlatform::YouTube: {
        }
        break;
    }

    for (const auto &check : *checks)
    {
        if (auto checkResult = check.cb(args, twitchBadges, senderName,
                                        originalMessage, messageFlags, self);
            checkResult)
        {
            highlighted = true;

            if (checkResult->alert)
            {
                if (!result.alert)
                {
                    result.alert = checkResult->alert;
                }
            }

            if (checkResult->playSound)
            {
                if (!result.playSound)
                {
                    result.playSound = checkResult->playSound;
                }
            }

            if (checkResult->customSoundUrl)
            {
                if (!result.customSoundUrl)
                {
                    result.customSoundUrl = checkResult->customSoundUrl;
                }
            }

            if (checkResult->color)
            {
                if (!result.color)
                {
                    result.color = checkResult->color;
                }
            }

            if (checkResult->showInMentions)
            {
                if (!result.showInMentions)
                {
                    result.showInMentions = checkResult->showInMentions;
                }
            }

            if (!checkResult->triggerWords.empty())
            {
                for (const auto &w : checkResult->triggerWords)
                {
                    if (!w.isEmpty() &&
                        std::find(result.triggerWords.begin(),
                                  result.triggerWords.end(),
                                  w) == result.triggerWords.end())
                    {
                        result.triggerWords.push_back(w);
                    }
                }
            }

            if (result.full())
            {
                break;
            }
        }
    }

    return {highlighted, result};
}

std::pair<bool, HighlightResult> HighlightController::check(
    const MessageParseArgs &args, const std::vector<TwitchBadge> &twitchBadges,
    const QString &senderName, const QString &originalMessage,
    const MessageFlags &messageFlags, MessagePlatform platform) const
{
    // Legacy overload: empty channel key resolves to the union of global
    // checks plus all "AllExcept {}" groups (i.e. everything). For users
    // with no groups configured, this is exactly the pre-feature behaviour.
    return this->check(args, twitchBadges, senderName, originalMessage,
                       messageFlags, platform, QString());
}

std::pair<bool, HighlightResult> HighlightController::check(
    const MessageParseArgs &args, const std::vector<TwitchBadge> &twitchBadges,
    const QString &senderName, const QString &originalMessage,
    const MessageFlags &messageFlags, MessagePlatform platform,
    const QString &channelKey) const
{
    // Use a NUL-containing sentinel key for the empty (legacy) case so it
    // can never collide with a real channel name from settings.json
    // channel lists. Because `AllExcept {}` groups match any key and
    // `Only` groups never match an unlisted key, the empty key resolves to
    // "global checks + every AllExcept {} group" -- the legacy behaviour.
    static const QString legacyKey = QStringLiteral("__limerino_legacy__\x01");

    const QString resolvedKey = channelKey.isEmpty() ? legacyKey : channelKey;

    auto resolved = this->resolveChecks(*getSettings(), resolvedKey);

    return this->runChecks(args, twitchBadges, senderName, originalMessage,
                           messageFlags, platform,  //
                           resolved);
}

}  // namespace chatterino
