# Sloperino [![GitHub Actions Build (Windows, Ubuntu, macOS, Flatpak)](https://github.com/w77hxhx/sloperino/actions/workflows/build.yml/badge.svg?branch=sloperino)](https://github.com/w77hxhx/sloperino/actions?query=workflow%3ABuild+branch%3Asloperino) [![Release](https://img.shields.io/github/v/release/w77hxhx/sloperino?include_prereleases)](https://github.com/w77hxhx/sloperino/releases/latest)

Sloperino is a feature-rich, high-performance fork of **Chatterino 2**, combining enhancements from [Chatterino7](https://github.com/SevenTV/chatterino7), [Leafyrino](https://github.com/leafyzito/leafyrino), [Technorino](https://github.com/2547techno/technorino), and [Moltorino](https://github.com/molt/moltorino), alongside exclusive features developed specifically for Sloperino.

---

### Features of Sloperino

- 👑 **Roles Lookup ([roles.tv](https://roles.tv))**:
  - **Channel Mode**: Browse and search all moderators, VIPs, artists, founders, and subscribers for any channel.
  - **User Mode**: Discover all Twitch channels where a specific user holds moderator, VIP, artist, or founder roles.
  - **Quick Access**: Dedicated `roles` button in usercards and split header menus.
  - **Live Search & Infinite Scroll**: Fast client-side filtering and smooth cursor-based pagination.

- 🎬 **Twitch Clips Manager**:
  - **Broadcaster & Curator Views**: Switch between clips made on a streamer's channel and clips created by a curator.
  - **Interactive Clip Cards**: Previews with duration badges, view counts, relative dates, game tags, and direct playback links.
  - **Search & Filtering**: Real-time search across titles, games, and curators.

- 🌐 **Chat Firehose & User Stalking**:
  - **Multi-Stream Firehose**: Stream live chat messages across public Twitch log feeds (_Spanix, Supa, Susgee, Nadeko, Logxx, Catquery_).
  - **Stalk Channels**: Track any Twitch user's chat messages across all public channels in real-time.
  - **Velocity Metrics**: Built-in Messages-Per-Second (MPS) tracking and real-time buffer statistics.

- ⚙️ **Usercard & Chat Customization**:
  - **Customizable Buttons**: Toggle `clips`, `roles`, `badges`, and `7TV` buttons in settings.

---

### Screenshots

<!-- Add your showcase screenshots here -->

#### 1. Roles Lookup (Channel & User Modes)

![Roles Lookup](https://github.com/w77hxhx/sloperino/raw/sloperino/screenshots/roles_dialog.png)

#### 2. Twitch Clips Manager

![Twitch Clips](https://github.com/w77hxhx/sloperino/raw/sloperino/screenshots/clips_dialog.png)

#### 3. Real-Time Firehose & Stalk Tab

![Firehose and Stalk](https://github.com/w77hxhx/sloperino/raw/sloperino/screenshots/firehose_stalk.png)

#### 4. Sloperino Settings Page

![Sloperino Settings](https://github.com/w77hxhx/sloperino/raw/sloperino/screenshots/settings_sloperino.png)

---

### Downloads

**Latest releases** can be downloaded from the [Releases section](https://github.com/w77hxhx/sloperino/releases/latest).

- **Windows**: `.zip` portable and installer `.exe`
- **Linux**: `.AppImage`, `.deb`, and Flatpak bundle
- **macOS**: `.dmg` Universal binary

---

### Issues & Feedback

If you encounter bugs, crashes, or feature requests regarding Sloperino-specific features (_Roles, Clips, Firehose, Stalk, 7TV_), please submit an issue [in our issue tracker](https://github.com/w77hxhx/sloperino/issues).

---

### Building Sloperino

#### Windows (CMake / MSVC / Ninja)

```shell
git clone --recurse-submodules https://github.com/w77hxhx/sloperino.git
cd sloperino
cmake -B build -DCMAKE_BUILD_TYPE=Release
cmake --build build --config Release
```

#### Linux (Ubuntu / Debian / Arch)

```shell
git clone --recurse-submodules https://github.com/w77hxhx/sloperino.git
cd sloperino
cmake -B build -DCMAKE_BUILD_TYPE=Release
cmake --build build --config Release -j$(nproc)
```

#### macOS (Clang / Homebrew Qt6)

```shell
git clone --recurse-submodules https://github.com/w77hxhx/sloperino.git
cd sloperino
cmake -B build -DCMAKE_BUILD_TYPE=Release -DCMAKE_PREFIX_PATH="$(brew --prefix qt@6)"
cmake --build build --config Release -j$(sysctl -n hw.ncpu)
```

---

### License

Sloperino is licensed under the [MIT License](LICENSE).
