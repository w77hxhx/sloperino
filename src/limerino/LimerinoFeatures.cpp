// SPDX-License-Identifier: MIT
// The Limerino feature catalog. Every Implemented entry corresponds to code
// that exists; tests/src/LimerinoFeatures.cpp enforces that commands listed
// here are registered and vice versa.

#include "limerino/LimerinoFeatures.hpp"

namespace chatterino::limerino {

namespace {

QList<LimerinoFeature> buildCatalog()
{
    return {
        // ---------------------------------------------------------- Setup --
        {
            QStringLiteral("extra-features-auth"),
            QStringLiteral("Extra-features sign-in"),
            QStringLiteral("Setup"),
            QStringLiteral("Second Twitch authorization used only by Limerino "
                           "features (moderation, rewards, chat settings)"),
            QStringLiteral("Limerino features talk to Twitch with a second, "
                           "elevated authorization, stored separately from your "
                           "main account. Sign in once with the device flow: a "
                           "code is shown, you confirm it on twitch.tv, and the "
                           "dialog reports the account name, when it was "
                           "generated, and its status."),
            FeatureStatus::Implemented,
            {{AccessKind::Dialog,
              QStringLiteral("Settings > Limerino > Manage extra-features login...")}},
            {},
            {},
            QStringLiteral("Limerino"),
        },
        {
            QStringLiteral("events-channel"),
            QStringLiteral("Live event feed (/events)"),
            QStringLiteral("Setup"),
            QStringLiteral("Dedicated /events channel showing live moderation, "
                           "prediction, points, poll and raid events for your "
                           "extra-features account"),
            QStringLiteral("Opens like /mentions or /whispers: pick \"Events\" "
                           "in the new-split dialog. Events render as proper "
                           "messages with a coloured category chip; the chip "
                           "tooltip holds the raw event. Right-click an event "
                           "to copy the pretty-printed raw payload. Channel "
                           "ids in moderation lines resolve to logins. Identical "
                           "wire events are deduplicated (toggle under Settings "
                           "> Limerino). Filter which event types appear via "
                           "the split's three-dots menu. Tokens are checked "
                           "once at startup and whenever the Limerino settings "
                           "page is opened."),
            FeatureStatus::Implemented,
            {
                {AccessKind::Dialog,
                 QStringLiteral("New split dialog > Twitch > Events")},
                {AccessKind::Dialog,
                 QStringLiteral("Settings > Limerino > Open events channel")},
                {AccessKind::SettingsToggle,
                 QStringLiteral("Settings > Limerino > Deduplicate identical "
                                "events in /events")},
                {AccessKind::ContextMenu,
                 QStringLiteral("/events split > three-dots menu > Filter events...")},
                {AccessKind::ContextMenu,
                 QStringLiteral("/events message > Copy raw event")},
                {AccessKind::Automatic,
                 QStringLiteral("topic subscriptions run in the background")},
            },
            {},
            {QStringLiteral("Requires Limerino auth")},
            QStringLiteral("Limerino"),
        },

        // ------------------------------------------------- Chat features --
        {
            QStringLiteral("limerino-actions"),
            QStringLiteral("Limerino Actions: predictions, polls, rewards, appearance"),
            QStringLiteral("Chat features"),
            QStringLiteral("Window for creating live predictions and polls, "
                           "redeeming channel point rewards, and setting your "
                           "global badge + chat color"),
            QStringLiteral("Open it from the channel-points icon in the split "
                           "header. Predictions: create with drafts (last 5 "
                           "kept), make/lock/pay out/cancel with confirmations, "
                           "past history, live refresh. Polls: 2-5 options with "
                           "Twitch limits. Rewards: browse and redeem custom "
                           "rewards with your live points balance. Appearance: "
                           "pick a global badge and your chat color."),
            FeatureStatus::Implemented,
            {
                {AccessKind::ToolbarButton,
                 QStringLiteral("channel-points icon in the split header")},
            },
            {},
            {QStringLiteral("Requires Limerino auth")},
            QString(),
        },
        {
            QStringLiteral("name-history"),
            QStringLiteral("Name history"),
            QStringLiteral("Chat features"),
            QStringLiteral("Show a user's past usernames"),
            QStringLiteral("Looks up previous usernames via the logs.zonian.dev "
                           "service. Runs as a chat command or as a button on any "
                           "usercard; the dialog lists the names."),
            FeatureStatus::Implemented,
            {
                {AccessKind::Command, QStringLiteral("/namehistory <user|id:...>")},
                {AccessKind::Dialog,
                 QStringLiteral("usercard > \"Name history\" button")},
            },
            {QStringLiteral("/namehistory")},
            {},
            QString(),
        },
        {
            QStringLiteral("uid"),
            QStringLiteral("User ID lookup"),
            QStringLiteral("Chat features"),
            QStringLiteral("Show a user's Twitch ID"),
            QStringLiteral("Prints the user's numeric Twitch ID in chat, "
                           "resolved through the Twitch API."),
            FeatureStatus::Implemented,
            {
                {AccessKind::Command,
                 QStringLiteral("/uid [user] (default: this channel)")},
            },
            {QStringLiteral("/uid")},
            {QStringLiteral("Requires Limerino auth")},
            QString(),
        },
        {
            QStringLiteral("follow"),
            QStringLiteral("Follow channel"),
            QStringLiteral("Chat features"),
            QStringLiteral("Follow a channel"),
            QStringLiteral("Follows the given channel (or this one by default). "
                           "Replaces the stock /follow and is also in the "
                           "split's three-dots menu."),
            FeatureStatus::Implemented,
            {
                {AccessKind::Command,
                 QStringLiteral("/follow [user] (default: this channel)")},
                {AccessKind::ContextMenu,
                 QStringLiteral("split three-dots menu > Follow channel")},
            },
            {QStringLiteral("/follow")},
            {QStringLiteral("Requires Limerino auth")},
            QString(),
        },
        {
            QStringLiteral("followers-following"),
            QStringLiteral("Followers / following windows"),
            QStringLiteral("Chat features"),
            QStringLiteral("Browse your own channel's followers or following list"),
            QStringLiteral("The followers window is searchable, shows since-when "
                           "dates, and can block from a context menu; the "
                           "following window shows live dots, notifications, and "
                           "opens chats from a context menu. Both entries appear "
                           "in the split three-dots menu only on your own Twitch "
                           "channel when extra-features auth is available."),
            FeatureStatus::Implemented,
            {
                {AccessKind::ContextMenu,
                 QStringLiteral("split three-dots menu > View followers / View "
                                "following (own channel)")},
            },
            {},
            {QStringLiteral("Requires Limerino auth")},
            QString(),
        },
        {
            QStringLiteral("pins"),
            QStringLiteral("Pinned messages"),
            QStringLiteral("Chat features"),
            QStringLiteral("Pin, unpin and view the channel's pinned message"),
            QStringLiteral("Pin any message by id, send a message straight "
                           "into the pin, or unpin the latest one. Viewing "
                           "shows the pinned message in a banner at the top of "
                           "chat, tinted with the Highlights color."),
            FeatureStatus::Implemented,
            {
                {AccessKind::Command, QStringLiteral("/pinmessage <message id>")},
                {AccessKind::Command, QStringLiteral("/sendpinnedmessage <text>")},
                {AccessKind::Command, QStringLiteral("/unpin")},
                {AccessKind::Command, QStringLiteral("/viewpin")},
            },
            {QStringLiteral("/pinmessage"), QStringLiteral("/sendpinnedmessage"),
             QStringLiteral("/viewpin"), QStringLiteral("/getpin"),
             QStringLiteral("/unpin")},
            {QStringLiteral("Requires Limerino auth")},
            QString(),
        },
        {
            QStringLiteral("resub"),
            QStringLiteral("Resub notification"),
            QStringLiteral("Chat features"),
            QStringLiteral("Share your resub notification"),
            QStringLiteral("Sends your resub notification; silent on success "
                           "like the reference plugin."),
            FeatureStatus::Implemented,
            {
                {AccessKind::Command, QStringLiteral("/resub [message]")},
            },
            {QStringLiteral("/resub")},
            {QStringLiteral("Requires Limerino auth")},
            QString(),
        },
        {
            QStringLiteral("cheer"),
            QStringLiteral("Cheer bits"),
            QStringLiteral("Chat features"),
            QStringLiteral("Send a cheer"),
            QStringLiteral("Sends bits with an optional message."),
            FeatureStatus::Implemented,
            {
                {AccessKind::Command, QStringLiteral("/cheer <bits> [message]")},
            },
            {QStringLiteral("/cheer")},
            {QStringLiteral("Requires Limerino auth")},
            QString(),
        },
        {
            QStringLiteral("displayname"),
            QStringLiteral("Display name"),
            QStringLiteral("Chat features"),
            QStringLiteral("Update your display name capitalization"),
            QStringLiteral("Changes how your username is capitalized in chat."),
            FeatureStatus::Implemented,
            {
                {AccessKind::Command, QStringLiteral("/displayname <name>")},
            },
            {QStringLiteral("/displayname")},
            {QStringLiteral("Requires Limerino auth")},
            QString(),
        },
        {
            QStringLiteral("logsextended"),
            QStringLiteral("Extended chat logs"),
            QStringLiteral("Chat features"),
            QStringLiteral("Fetch a user's chat log to a paste"),
            QStringLiteral("Fetches the full log for a user into a paste on the "
                           "configured paste host. -id keeps message ids; "
                           "without it the id field is dropped from the query."),
            FeatureStatus::Implemented,
            {
                {AccessKind::Command,
                 QStringLiteral("/logsextended [-id] [user] [channel]")},
                {AccessKind::SettingsToggle,
                 QStringLiteral("Settings > Limerino > Paste host")},
            },
            {QStringLiteral("/logsextended")},
            {QStringLiteral("Uses the configured paste host")},
            QStringLiteral("Limerino"),
        },

        // ----------------------------------------------------- Moderation --
        {
            QStringLiteral("modlist"),
            QStringLiteral("Moderated channels"),
            QStringLiteral("Moderation"),
            QStringLiteral("List every channel your extra-features account "
                           "moderates"),
            QStringLiteral("Refreshes the moderated-channel list from Twitch "
                           "and shows it in a dialog."),
            FeatureStatus::Implemented,
            {
                {AccessKind::Command, QStringLiteral("/modlist")},
            },
            {QStringLiteral("/modlist"), QStringLiteral("/ml")},
            {QStringLiteral("Requires Limerino auth")},
            QString(),
        },
        {
            QStringLiteral("modlogs"),
            QStringLiteral("Mod logs"),
            QStringLiteral("Moderation"),
            QStringLiteral("Per-moderator action breakdown"),
            QStringLiteral("Shows who did what in the channel: a per-moderator "
                           "breakdown (clickable names) and the full action "
                           "list, newest first, for a user or the channel over "
                           "a day range."),
            FeatureStatus::Implemented,
            {
                {AccessKind::Command,
                 QStringLiteral("/modlogs [user] [days|all]")},
            },
            {QStringLiteral("/modlogs")},
            {QStringLiteral("Requires Limerino auth")},
            QString(),
        },
        {
            QStringLiteral("chat-warning-acknowledge"),
            QStringLiteral("Chat warning acknowledge"),
            QStringLiteral("Moderation"),
            QStringLiteral("Acknowledge chat warnings from mods"),
            QStringLiteral("When you are warned, an [acknowledge] link appears "
                           "on the warning message and in the /events feed. "
                           "You can also acknowledge via command, or let the "
                           "client acknowledge every warning automatically."),
            FeatureStatus::Implemented,
            {
                {AccessKind::Command, QStringLiteral("/acknowledgewarning")},
                {AccessKind::ContextMenu,
                 QStringLiteral("[acknowledge] link on warning messages")},
                {AccessKind::SettingsToggle,
                 QStringLiteral("Settings > Limerino > Automatically acknowledge "
                                "chat warnings")},
            },
            {QStringLiteral("/acknowledgewarning")},
            {QStringLiteral("Requires Limerino auth")},
            QStringLiteral("Limerino"),
        },
        {
            QStringLiteral("roles"),
            QStringLiteral("Artist & lead-mod roles"),
            QStringLiteral("Moderation"),
            QStringLiteral("Grant artist or lead-mod on your own channel"),
            QStringLiteral("Assign the artist role (and revoke it) or the lead "
                           "moderator role on your own channel - by command or "
                           "from icon buttons on the user's card."),
            FeatureStatus::Implemented,
            {
                {AccessKind::Command, QStringLiteral("/artist <user>")},
                {AccessKind::Command, QStringLiteral("/unartist <user>")},
                {AccessKind::Command, QStringLiteral("/leadmod <user>")},
                {AccessKind::Dialog,
                 QStringLiteral("usercard > Artist / Unartist / Lead mod icons "
                                "(own channel only)")},
            },
            {QStringLiteral("/artist"), QStringLiteral("/unartist"),
             QStringLiteral("/leadmod")},
            {QStringLiteral("Requires Limerino auth"),
             QStringLiteral("Own channel only")},
            QString(),
        },
        {
            QStringLiteral("nuke"),
            QStringLiteral("Message nuke"),
            QStringLiteral("Moderation"),
            QStringLiteral("Moderator emergency message purge"),
            QStringLiteral("Preview (with buffer-coverage banner), then delete "
                           "or timeout messages matching content and/or sender "
                           "(regex or literal), with undo (/unnuke) and cancel "
                           "(/cancelnuke). Named presets are saved for reuse. "
                           "Deletes cannot be undone and the dialog says so "
                           "next to the action selection."),
            FeatureStatus::Implemented,
            {
                {AccessKind::ContextMenu,
                 QStringLiteral("split three-dots menu > Nuke messages... "
                                "(moderators)")},
                {AccessKind::Command, QStringLiteral("/cancelnuke")},
                {AccessKind::Command, QStringLiteral("/unnuke")},
            },
            {QStringLiteral("/cancelnuke"), QStringLiteral("/unnuke")},
            {QStringLiteral("Requires Limerino auth"),
             QStringLiteral("Requires moderator")},
            QString(),
        },

        // ---------------------------------------------------- Highlights --
        {
            QStringLiteral("highlight-groups"),
            QStringLiteral("Highlight groups"),
            QStringLiteral("Highlights"),
            QStringLiteral("Attach highlights to channel groups and switch them "
                           "per channel"),
            QStringLiteral("Every phrase, user and badge highlight belongs to a "
                           "group; the group decides which channels it applies "
                           "to (everywhere, all except, or only listed). Manage "
                           "groups from Settings > Highlights; the split's "
                           "three-dots menu shows which groups apply there."),
            FeatureStatus::Implemented,
            {
                {AccessKind::SettingsToggle,
                 QStringLiteral("Settings > Highlights > Group column + "
                                "\"Manage groups...\"")},
                {AccessKind::ContextMenu,
                 QStringLiteral("split three-dots menu > Highlight groups")},
            },
            {},
            {},
            QStringLiteral("Limerino"),
        },

        // ---------------------------------------------------- User info ---
        {
            QStringLiteral("usercard-extras"),
            QStringLiteral("Usercard details (language, team, subscription)"),
            QStringLiteral("User info"),
            QStringLiteral("Extra facts on Twitch usercards"),
            QStringLiteral("Usercards show the user's preferred language tag, "
                           "primary Twitch team, and subscription detail "
                           "(months, Prime/gift from <name>, tier, platform, "
                           "SKU) without you doing anything."),
            FeatureStatus::Implemented,
            {
                {AccessKind::Automatic,
                 QStringLiteral("shown automatically on Twitch usercards")},
            },
            {},
            {QStringLiteral("Requires Limerino auth")},
            QString(),
        },
        {
            QStringLiteral("seventv-editors"),
            QStringLiteral("7TV editor lists"),
            QStringLiteral("User info"),
            QStringLiteral("View a user's 7TV editors"),
            QStringLiteral("From a usercard's avatar menu: list that user's "
                           "7TV editors, or the channels somebody edits in; "
                           "rows link to their 7TV user pages."),
            FeatureStatus::Implemented,
            {
                {AccessKind::ContextMenu,
                 QStringLiteral("usercard avatar right-click > View user "
                                "editors (7TV) / View editor-in-channels (7TV)")},
            },
            {},
            {},
            QString(),
        },

        // --------------------------------------------------- Automation ---
        {
            QStringLiteral("auto-actions"),
            QStringLiteral("Auto actions"),
            QStringLiteral("Automation"),
            QStringLiteral("Rules that fire automatically on incoming messages"),
            QStringLiteral("Each rule matches message text and/or sender "
                           "(regex or plain, case-insensitive by default) and "
                           "sends an action with placeholders like "
                           "{sender.name}, {msg.id} or {channel.name}. Rules "
                           "run in the background; the Auto Actions settings "
                           "tab lists and edits them."),
            FeatureStatus::Implemented,
            {
                {AccessKind::Dialog,
                 QStringLiteral("Settings > Auto Actions > Add rule...")},
                {AccessKind::Automatic,
                 QStringLiteral("rules fire on matching messages without input")},
            },
            {},
            {QStringLiteral("Requires Limerino auth for sending")},
            QStringLiteral("Limerino"),
        },

        // -------------------------------------------------- Appearance ---
        {
            QStringLiteral("theme-creator"),
            QStringLiteral("Theme creator"),
            QStringLiteral("Appearance"),
            QStringLiteral("Recolor a real Chatterino theme from four colors"),
            QStringLiteral("Pick a base (Dark/Light/Black/White.json or a "
                           "recent saved theme) and four seed colours. Matching "
                           "RGB values in the base file are replaced; other "
                           "leaves (links, system text, tab banding, …) stay "
                           "as in that JSON. Live preview uses theme "
                           "auto-reload. Apply installs Themes/<Name>.json, "
                           "selects it, and adds it to Recents in the creator. "
                           "Export a Limerino seed or a full themes.json. "
                           "Font family/size/weight stay in Settings > "
                           "Appearance, not in the theme."),
            FeatureStatus::Implemented,
            {
                {AccessKind::Dialog,
                 QStringLiteral("Settings > Limerino > Theme creator > "
                                "Create theme...")},
            },
            {},
            {},
            QStringLiteral("Limerino"),
        },

        // --------------------------------------------------- Moderation ---
        {
            QStringLiteral("crossban"),
            QStringLiteral("Crossban"),
            QStringLiteral("Moderation"),
            QStringLiteral("Ban, timeout, or unban a user across a channel preset"),
            QStringLiteral("Opens from the usercard. Default preset is every "
                           "channel you currently moderate (live from Limerino "
                           "auth, not a snapshot). Custom presets can pin a "
                           "fixed list, including channels you don't mod "
                           "(those rows show an auth/mod error). Refresh live "
                           "strike status per channel, act with a shared reason, "
                           "and read or add mod notes. Ban/timeout/unban use the "
                           "primary Twitch account (same as Nuke); status and "
                           "notes use Limerino extra-features auth."),
            FeatureStatus::Implemented,
            {
                {AccessKind::Dialog,
                 QStringLiteral("usercard > Crossban")},
            },
            {},
            {QStringLiteral("Requires Limerino auth covering the channels; "
                            "primary account needs ban rights")},
            QString(),
        },
    };
}

}  // namespace

const QList<LimerinoFeature> &limerinoFeatures()
{
    static const QList<LimerinoFeature> catalog = buildCatalog();
    return catalog;
}

const LimerinoFeature *findLimerinoFeature(const QString &id)
{
    for (const auto &f : limerinoFeatures())
    {
        if (f.id == id)
        {
            return &f;
        }
    }
    return nullptr;
}

}  // namespace chatterino::limerino
