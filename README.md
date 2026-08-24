# Sloperino [![GitHub Actions Build](https://github.com/w77hxhx/sloperino/actions/workflows/build.yml/badge.svg?branch=sloperino)](https://github.com/w77hxhx/sloperino/actions?query=workflow%3ABuild+branch%3Asloperino) [![Release](https://img.shields.io/github/v/release/w77hxhx/sloperino?include_prereleases)](https://github.com/w77hxhx/sloperino/releases/latest)

<div align="center">
  <p><strong>The ultimate high-performance Twitch chat client built for power users, moderators, and enthusiasts.</strong></p>
</div>

---

## 🌟 Exclusive Features of Sloperino

Sloperino introduces powerful, unique features and integrations not found in standard Chatterino:

### 👑 1. Full [roles.tv](https://roles.tv) Roles Lookup Suite
- **User Mode**: Instantly look up every Twitch channel where a user has **Moderator**, **VIP**, **Artist**, or **Founder** status.
- **Channel Mode**: Browse and search all staff, mods, VIPs, artists, and subscribers for any Twitch channel.
- **One-Click Actions**: Dedicated `roles` button in usercards and split headers with fast client-side filtering and infinite cursor pagination.

### 🎬 2. Native Twitch Clips Suite
- **Broadcaster & Curator Views**: Seamlessly switch between clips created on a streamer's channel and clips clipped by a specific curator.
- **Smooth Auto-Pagination**: Infinite scrolling that automatically streams more clips as you scroll down without losing your position.
- **Rich Media Cards**: Duration badges, view metrics, relative timestamps, colored category/game tags, and instant launch into your default player/browser.
- **Full Text Search**: Instant real-time filtering across titles, games, and curators.

### ⚡ 3. Multi-Feed Chat Firehose & Real-Time Stalking
- **Aggregated Firehose**: Streams global Twitch chat directly through high-speed public logs (*Spanix, Supa, Susgee, Nadeko, Logxx, Catquery*).
- **Zero-Allocation Deduplication**: Sharded cache algorithm ensuring ultra-low CPU and RAM usage during heavy traffic bursts.
- **5-Minute Auto-Reconnection**: Periodic full reconnection ensures reliable streaming without silent socket dropouts.
- **Global User Stalking**: Track any user's messages across all public Twitch channels live in a dedicated stalk tab.
- **MPS Velocity Meter**: Real-time Messages-Per-Second throughput tracking in header titles.

### 🎨 4. 7TV Cosmetics Studio & Live Presence Beaconing
- **Paints & Badges Picker**: Dedicated 7TV button on the chat input toolbar opening an interactive cosmetics studio.
- **Live Preview**: See your active 7TV Paint gradient, shadow, and badges rendered directly on your username in real time.
- **GraphQL Mutation Syncing**: Equip and switch 7TV paints and badges directly from the client.
- **Channel Presence Reporting**: Automatically beacons your active Twitch chat channel to 7TV presence servers (`7tv.io/v3/users/{userId}/presences`).
- **7TV Legacy Token Extraction**: One-click script generator for fast authentication via the 7TV Web Console.

### 🎯 5. Chat Precision & Moderator Protections
- **Smart Mention Highlights**: Exact trigger word matching for emote mentions with highlight pills, while strictly preventing `#channel` names, timestamps, and badges from falsely highlighting.
- **Mod Channel History Protection**: Prevents chat history spam when connecting to moderated channels, while keeping manual log search completely functional.
- **Universal "Go to Message"**: Navigate directly to the corresponding chat message from historical logs, usercard logs, and search popups.
- **Emote Offset Accuracy in Logs**: Pixel-perfect alignment for Twitch emotes in usercards and log searches by properly handling reply mentions.

### 📌 6. Always-on-Top Pinned Windows
- Click the pin button on **Clips, Roles, 7TV Cosmetics, Badges, Usercards, and Polls** to keep them pinned **Always on Top** of all other application windows.

### 🎛️ 7. Sloperino Control Center (Settings -> Sloperino)
- Dedicated settings tab to toggle usercard buttons, firehose streaming endpoints, 7TV authentication, and performance limits all in one centralized hub.

---

## 📸 Screenshots

#### 1. Roles Lookup ([roles.tv](https://roles.tv))
![Roles Lookup](https://github.com/w77hxhx/sloperino/raw/sloperino/screenshots/roles_dialog.png)

#### 2. Twitch Clips Manager
![Twitch Clips](https://github.com/w77hxhx/sloperino/raw/sloperino/screenshots/clips_dialog.png)

#### 3. Real-Time Firehose & Stalk Tab
![Firehose and Stalk](https://github.com/w77hxhx/sloperino/raw/sloperino/screenshots/firehose_stalk.png)

#### 4. Sloperino Settings Control Center
![Sloperino Settings](https://github.com/w77hxhx/sloperino/raw/sloperino/screenshots/settings_sloperino.png)

---

## 📦 Downloads & Releases

Pre-built binaries for all major operating systems are available under the **[Releases](https://github.com/w77hxhx/sloperino/releases/latest)** section:

- 🪟 **Windows**: Portable `.zip` and Installer `.exe` (x64)
- 🐧 **Linux**: `.AppImage`, `.deb` package, and Flatpak bundle
- 🍏 **macOS**: Universal binary `.dmg` (Apple Silicon & Intel)

---

## 🛠️ Building from Source

### Prerequisites
- CMake 3.20+
- Qt 6.5+ (Core, Gui, Widgets, Network, Svg, Concurrent)
- C++20 compatible compiler (MSVC 2022 / GCC 12+ / Clang 15+)

### Clone & Build
```shell
git clone --recurse-submodules https://github.com/w77hxhx/sloperino.git
cd sloperino
cmake -B build -DCMAKE_BUILD_TYPE=Release
cmake --build build --config Release -j
```

---

## 📄 License

Sloperino is licensed under the [MIT License](LICENSE).
