# Sloperino [![GitHub Actions Build](https://github.com/w77hxhx/sloperino/actions/workflows/build.yml/badge.svg?branch=sloperino)](https://github.com/w77hxhx/sloperino/actions?query=workflow%3ABuild+branch%3Asloperino) [![Release](https://img.shields.io/github/v/release/w77hxhx/sloperino?include_prereleases)](https://github.com/w77hxhx/sloperino/releases/latest)

Just a fork of features I like from other forks + my own features :)

Features taken from:

- https://github.com/leafyzito/leafyrino (base)
- Moltorino
- https://github.com/2547techno/technorino
- https://github.com/seventv/chatterino7
- https://github.com/lagx/Limerino

---

## My own features (Sloperino)

- **Multi-feed Twitch Firehose** — global chat streamed through public log services (Spanix, Supa, Susgee, Nadeko, Logxx, Catquery) with sharded deduplication and a live messages-per-second meter
- **Stalk channel** — follow any user's messages across all of Twitch in real time, with persistent caching across restarts
- **Roles lookup suite** — browse every channel where a user is mod / VIP / artist / founder via [roles.tv](https://roles.tv), or list the staff of any channel; buttons in usercards and split headers
- **Twitch clips manager** — clips by broadcaster or curator, infinite auto-pagination, full text search
- **7TV cosmetics studio** — equip paints & badges with live preview directly on your username, plus presence reporting to 7TV
- **Emote aliases** — replace words in chat with 7TV / BTTV / FFZ / CDN emote links
- **Random client-nonce mode** — simulate Web / iOS / Android clients when sending messages
- Smart mention highlights for emote-triggered mentions (no false positives from channel names or timestamps)
- Pin any dialog (clips, roles, usercards, polls...) always-on-top

## From Moltorino

- Multi-account device login (`auth.molto.lol`)
- Pinned message banner with timers, `/pin` commands and pin notifications
- Prediction & poll banners, dialogs and moderation actions
- Channel points balance display and rewards picker
- Bot badge mode (`/bot`) with its own account
- Incoming/outgoing message translation
- Supporter badges, modlog lookup, `/spam`, `/pyramid`
- Repeated-message detector with inline counters

## From Technorino

- `#channel` text converted to clickable links
- Fake webchat messages, bot rate limits for messages and JOINs
- Client detection highlights
- Watching-tab live sound, auto-detach watching tab
- Experimental Markdown parsing

## From Chatterino7

- 7TV subscriber features: name paints, personal emotes, animated avatars
- Kick.com support (chat, emotes, badges, accounts)

## From Limerino

- **Highlight groups** — scope any highlight phrase, user or badge to specific channels via the new Group column and "Manage groups..." dialog
- **Auto Actions** — rule engine that reacts to incoming messages (own tab in settings)
- **`/events` channel** — live feed of your mod actions, raids, predictions and channel points over Hermes PubSub, with per-event-type filters
- **Prediction manager** — create, vote and manage predictions from the client
- **Nuke dialog** — visual `/nuke` with match preview before execution
- **Crossban presets** — ban a user across all your moderated channels
- **Theme creator** — generate and install custom themes from the client
- Extra commands: `/uid`, `/namehistory`, `/modlist`, follower/following lists
- Usercard extras: name history, crossban button, artist / lead-mod actions, 7TV editor lookups

---

## Downloads

Pre-built binaries are available under the **[Releases](https://github.com/w77hxhx/sloperino/releases/latest)** section:

- Windows: portable `.zip` and installer `.exe` (x64)
- Linux: `.AppImage`, `.deb` and Flatpak
- macOS: universal `.dmg`

Nightly builds are published on every push to the `sloperino` branch.

## Building from source

```shell
git clone --recurse-submodules https://github.com/w77hxhx/sloperino.git
cd sloperino
cmake -B build -DCMAKE_BUILD_TYPE=Release
cmake --build build --config Release -j
```

See the upstream guides for platform-specific dependencies: [Windows](BUILDING_ON_WINDOWS.md), [Linux](BUILDING_ON_LINUX.md), [macOS](BUILDING_ON_MAC.md), [FreeBSD](BUILDING_ON_FREEBSD.md).

## License

Licensed under the [MIT License](LICENSE).
