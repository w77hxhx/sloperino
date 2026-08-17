// SPDX-FileCopyrightText: 2026 Contributors to Chatterino <https://chatterino.com>
//
// SPDX-License-Identifier: MIT

#include "widgets/settingspages/LeafyrinoPage.hpp"

#include "singletons/Settings.hpp"
#include "widgets/BaseWidget.hpp"
#include "widgets/settingspages/GeneralPageView.hpp"
#include "widgets/settingspages/SettingWidget.hpp"

#include <QFrame>
#include <QHBoxLayout>
#include <QVBoxLayout>

namespace chatterino {

LeafyrinoPage::LeafyrinoPage()
{
    auto *outer = new QVBoxLayout;
    auto *inner = new QHBoxLayout;
    auto *view = GeneralPageView::withNavigation(this);
    this->view_ = view;

    inner->addWidget(view);
    auto *frame = new QFrame;
    frame->setLayout(inner);
    outer->addWidget(frame);
    this->setLayout(outer);

    this->initLayout(*view);
}

bool LeafyrinoPage::filterElements(const QString &query)
{
    if (this->view_)
    {
        return this->view_->filterElements(query) || query.isEmpty();
    }

    return false;
}

void LeafyrinoPage::initLayout(GeneralPageView &layout)
{
    auto &s = *getSettings();

    // 1. Usercard Category
    layout.addTitle("Usercard");
    layout.addDescription("Customize usercard buttons, details, and widgets.");

    SettingWidget::checkbox("Show clips button", s.showUsercardClipsButton)
        ->setTooltip("Show a button on usercards to view and search the user's "
                     "Twitch clips.")
        ->addKeywords({"usercard", "clips", "twitch", "button", "video"})
        ->addTo(layout);

    SettingWidget::checkbox("Show badges button", s.showUsercardBadgesButton)
        ->setTooltip("Show a button on usercards to view earned Twitch badges "
                     "in the channel.")
        ->addKeywords({"usercard", "badges", "twitch", "button"})
        ->addTo(layout);

    SettingWidget::checkbox("Show follower count", s.showUsercardFollowerCount)
        ->addKeywords({"usercard", "follower", "count"})
        ->addTo(layout);

    SettingWidget::checkbox("Show account creation date",
                            s.showUsercardCreatedDate)
        ->addKeywords({"usercard", "created", "account", "date"})
        ->addTo(layout);

    SettingWidget::checkbox("Show last live", s.showUsercardLastLive)
        ->setTooltip("Show when the user was last live. Hover the row to see "
                     "the stream title.")
        ->addKeywords({"usercard", "last", "live", "stream"})
        ->addTo(layout);

    SettingWidget::checkbox("Show live viewer count",
                            s.showUsercardLiveViewerCount)
        ->setTooltip("When enabled, replaces the red live dot next to the "
                     "username with the live viewer count.")
        ->addKeywords({"usercard", "live", "viewer", "count", "indicator"})
        ->addTo(layout);

    SettingWidget::checkbox("Show user color", s.showUsercardColor)
        ->setTooltip("Show the user's Twitch chat color.")
        ->addKeywords({"usercard", "color", "chat"})
        ->addTo(layout);

    SettingWidget::checkbox("Show 7TV paint", s.showUsercardSevenTVPaint)
        ->setTooltip("Show the user's equipped 7TV paint on the usercard.")
        ->addKeywords({"usercard", "7tv", "seventv", "paint", "cosmetic"})
        ->addTo(layout);

    SettingWidget::checkbox("Show 7TV profile button",
                            s.showSevenTVUsercardButton)
        ->setTooltip("Show a button to view the user's 7TV profile.")
        ->addKeywords({"usercard", "7tv", "profile", "button"})
        ->addTo(layout);

    SettingWidget::checkbox("Show Twitch status", s.showUsercardStatus)
        ->setTooltip(
            "Show whether the user is Staff, Partner, Affiliate, or Regular.")
        ->addKeywords({"usercard", "status", "staff", "partner", "affiliate"})
        ->addTo(layout);

    SettingWidget::checkbox("Show chatter count", s.showUsercardChatterCount)
        ->setTooltip("Show the current chatter count when available.")
        ->addKeywords({"usercard", "chatter", "count"})
        ->addTo(layout);

    SettingWidget::checkbox("Show followage", s.showUsercardFollowage)
        ->addKeywords({"usercard", "followage", "follow"})
        ->addTo(layout);

    SettingWidget::checkbox("Show followage relative time",
                            s.showUsercardFollowageRelativeTime)
        ->conditionallyEnabledBy(s.showUsercardFollowage)
        ->setTooltip("Show how long ago the user followed relative to now.")
        ->addKeywords({"usercard", "followage", "relative", "time"})
        ->addTo(layout);

    SettingWidget::checkbox("Show subage", s.showUsercardSubage)
        ->addKeywords({"usercard", "subage", "subscriber", "sub"})
        ->addTo(layout);

    SettingWidget::checkbox("Show subage relative time",
                            s.showUsercardSubageRelativeTime)
        ->conditionallyEnabledBy(s.showUsercardSubage)
        ->setTooltip("Show subscription duration in relative time.")
        ->addKeywords({"usercard", "subage", "relative", "time"})
        ->addTo(layout);

    SettingWidget::checkbox("Show follow button", s.showFollowButtonInUsercard)
        ->setTooltip("Show a follow/unfollow button on usercards. Requires "
                     "Moltorino auth (Settings → Moltorino → Authentication).")
        ->addKeywords({"usercard", "follow", "button"})
        ->addTo(layout);

    SettingWidget::checkbox("Confirm before unfollowing from usercard",
                            s.confirmUnfollowFromUsercard)
        ->conditionallyEnabledBy(s.showFollowButtonInUsercard)
        ->setTooltip("Ask before the usercard follow button unfollows a user. "
                     "The /unfollow command still runs without a prompt.")
        ->addKeywords({"usercard", "follow", "unfollow", "confirm"})
        ->addTo(layout);

    SettingWidget::checkbox("Show gift sub gifter", s.showUsercardSubGiftGifter)
        ->setTooltip("When the user has an active gifted subscription in a "
                     "channel, show who gifted it on the usercard.")
        ->addKeywords({"usercard", "gift", "gifter", "subscription", "sub"})
        ->addTo(layout);

    SettingWidget::checkbox("Show name history button",
                            s.showUsercardNameHistoryButton)
        ->setTooltip(
            "Show previous Twitch usernames (aka button) on usercards.")
        ->addKeywords({"usercard", "aka", "history", "names"})
        ->addTo(layout);

    SettingWidget::checkbox("Show load more messages button",
                            s.showUsercardLoadMoreMessagesButton)
        ->setTooltip("Show button to fetch older messages sent by this user.")
        ->addKeywords({"usercard", "messages", "load", "history"})
        ->addTo(layout);

    SettingWidget::checkbox("Always load more messages automatically",
                            s.alwaysLoadMoreUsercardMessages)
        ->setTooltip(
            "Automatically fetch full message history on opening usercard.")
        ->addKeywords({"usercard", "messages", "auto", "load"})
        ->addTo(layout);

    SettingWidget::checkbox("Show lead moderator role buttons",
                            s.showLeadModRoleButtons)
        ->setTooltip("Show Lead Moderator management buttons for mods/vips.")
        ->addKeywords({"usercard", "lead", "mod", "moderator", "role"})
        ->addTo(layout);

    SettingWidget::checkbox("Show role management menu",
                            s.showUsercardRoleManagementMenu)
        ->setTooltip("Show role management context menu in usercard.")
        ->addKeywords({"usercard", "roles", "mod", "vip", "management"})
        ->addTo(layout);

    // 2. Badges Category
    layout.addTitle("Badges");
    layout.addDescription("Toggle custom and third-party badges.");

    SettingWidget::checkbox("Show Sloperino badges", s.showSloperinoBadges)
        ->addKeywords({"badges", "sloperino"})
        ->addTo(layout);
    SettingWidget::checkbox("Show Folhinha badges", s.showBadgesFolhinha)
        ->addKeywords({"badges", "folhinha"})
        ->addTo(layout);
    SettingWidget::checkbox("Show Homies supporter badges",
                            s.showBadgesHomiesSupporter)
        ->addKeywords({"badges", "homies", "supporter"})
        ->addTo(layout);
    SettingWidget::checkbox("Show Homies custom badges",
                            s.showBadgesHomiesCustom)
        ->addKeywords({"badges", "homies", "custom"})
        ->addTo(layout);
    SettingWidget::checkbox("Show Moltorino badges", s.showBadgesMoltorino)
        ->addKeywords({"badges", "moltorino"})
        ->addTo(layout);
    SettingWidget::checkbox("Show Technorino badges", s.showTechnorinoBadges)
        ->addKeywords({"badges", "technorino"})
        ->addTo(layout);
    SettingWidget::checkbox("Show donor badges", s.showDonorBadges)
        ->addKeywords({"badges", "donor"})
        ->addTo(layout);
    SettingWidget::checkbox("Show 7TV badges", s.showBadgesSevenTV)
        ->addKeywords({"badges", "7tv", "seventv"})
        ->addTo(layout);
    SettingWidget::checkbox("Animate 7TV badges", s.animateSevenTVBadges)
        ->addKeywords({"badges", "7tv", "animated"})
        ->addTo(layout);

    // 3. Messages per second
    layout.addTitle("Messages per second");
    SettingWidget::checkbox("Show messages-per-second (mps) overlay in splits",
                            s.showSplitMps)
        ->setTooltip("Shows a faint overlay label (e.g. \"12 mps\") with the "
                     "average number of messages per second over the "
                     "selected window.")
        ->addTo(layout);

    SettingWidget::dropdown("MPS averaging window", s.splitMpsWindow)
        ->conditionallyEnabledBy(s.showSplitMps)
        ->setTooltip("Longer windows smooth out Twitch burst delivery so the "
                     "counter does not spike and drop to zero between bursts.")
        ->addTo(layout);

    SettingWidget::dropdown("MPS overlay position", s.splitMpsCorner)
        ->conditionallyEnabledBy(s.showSplitMps)
        ->addTo(layout);

    SettingWidget::checkbox("Show 0 mps", s.showSplitMpsWhenZero)
        ->conditionallyEnabledBy(s.showSplitMps)
        ->addTo(layout);

    // 5. Miscellaneous Category
    layout.addTitle("Miscellaneous");
    SettingWidget::checkbox("Use message colors for tab alerts",
                            s.colorTabHighlightsByMessage)
        ->setTooltip("When a message highlights a tab, use that highlight "
                     "color for the tab alert line.")
        ->addKeywords({"tab", "alert", "highlight", "color"})
        ->addTo(layout);
    SettingWidget::checkbox("Wrap links at breaks", s.wrapLinksAtBreaks)
        ->setTooltip("Let URLs wrap at /, ?, &, #, and = instead of staying "
                     "on one line.")
        ->addKeywords({"url", "link", "wrap", "break"})
        ->addTo(layout);
    SettingWidget::checkbox("Show full date when hovering timestamps",
                            s.showTimestampDateTooltip)
        ->setTooltip("Show the full message date and time when hovering a "
                     "timestamp. Uses your message timestamp format for the "
                     "time portion.")
        ->addKeywords({"timestamp", "date", "tooltip", "hover"})
        ->addTo(layout);

    // 6. Follow Events Category
    layout.addTitle("Follow events");
    layout.addDescription(
        "Shows \"X followed the channel.\" system messages in chat. Only "
        "works in channels where you are the broadcaster or a moderator. "
        "Requires Moltorino auth (Settings → Moltorino → Authentication), "
        "experimental EventSub enabled under Settings → General, and an app "
        "restart after turning EventSub on.");
    SettingWidget::checkbox("Show follow events in chat",
                            s.showFollowEventsInChat)
        ->addKeywords({"follow", "events", "eventsub", "chat", "moderator"})
        ->addTo(layout);

    layout.addStretch();

    // Invisible element for width
    auto *inv = new BaseWidget(this);
    layout.addWidget(inv);
}

}  // namespace chatterino
