# Sloperino

**Sloperino** is a feature-rich, high-performance chat client for [Twitch.tv](https://twitch.tv), based on [Chatterino 2](https://github.com/Chatterino/chatterino2).

> [!NOTE]
> This project is a fork of [**leafyzito/leafyrino**](https://github.com/leafyzito/leafyrino) combining enhancements from [Technorino](https://github.com/2547techno/technorino), [Chatterino7](https://github.com/itzAlex/chatterino7), and [Moltorino](https://github.com/molt/moltorino), alongside custom features developed for Sloperino.

---

## ✨ Features

### 🎬 Twitch Clips Manager

- **Usercard Clips Button**: Open any Twitch user's clips catalog directly from their usercard (`UserInfoPopup`).
- **Broadcaster & Curator Views**: Switch between clips created on the broadcaster's channel and clips curated/clipped by the user.
- **Search & Filtering**: Instant search across clip titles, game categories, broadcasters, and curators.
- **Auto-Pagination**: Seamless infinite scrolling with cursor-based pagination.
- **Clip Cards**: Preview thumbnails with duration badge overlay, formatted view counts, relative dates, categories, and direct browser / incognito links with clipboard actions.

### 🌐 Real-Time Chat Firehose

- **Multi-Feed Streaming**: Connect to public WebSocket firehose streams (`Spanix`, `Supa`, `Susgee`, `Nadeko`, `Logxx`, `Catquery`).
- **Stream Rate & Metrics**: Monitor real-time message rates (`mps`) and buffer statistics in the tab header.
- **Performance Optimized**: Configurable batch rendering intervals and ring buffer storage.

### 🎨 7TV & Rich Media Integration

- **7TV Name Paints & Animated Profile Avatars**: Full support for personal cosmetic paints and animated avatars in usercards and chat.
- **7TV Personal Emotes & 4x Emote Scaling**: High-resolution emote rendering for 7TV, FFZ, and BTTV.
- **YouTube Inline Previews**: Optional hover autoplay and playback controls for shared YouTube links.

### ⚙️ Granular Usercard & Chat Customization

- **Sloperino Settings Page**: Dedicated settings tab with a unified **Usercard** category to toggle clips button, badges button, follower counts, creation dates, last live status, live viewer counters, chat colors, paints, and follow buttons.
- **Messages Per Second (MPS) Overlay**: Subtle on-screen overlay showing average chat velocity.
- **Random Client Nonce Mode**: Experimental chat sending with randomized client identifiers.

---

## 🚀 Building Sloperino

### Prerequisites

- CMake (>= 3.16)
- Qt 6 (or Qt 5.15+) with SVG, Concurrent, Network, and Widgets modules
- C++20 compliant compiler (MSVC 2022 / GCC 11+ / Clang 13+)
- OpenSSL & Boost

### Build on Windows (with CMake / vcpkg)

```shell
git clone --recurse-submodules https://github.com/w77hxhx/sloperino.git
cd sloperino
cmake -B build -DCMAKE_BUILD_TYPE=Release
cmake --build build --config Release
```

For detailed platform-specific build instructions, refer to:

- [Building on Windows](BUILDING_ON_WINDOWS.md)
- [Building on Windows with vcpkg](BUILDING_ON_WINDOWS_WITH_VCPKG.md)
- [Building on Linux](BUILDING_ON_LINUX.md)
- [Building on macOS](BUILDING_ON_MAC.md)
- [Building on FreeBSD](BUILDING_ON_FREEBSD.md)

---

## 📜 Credits & Upstream Projects

- **Leafyrino**: [leafyzito/leafyrino](https://github.com/leafyzito/leafyrino)
- **Chatterino 2**: [Chatterino/chatterino2](https://github.com/Chatterino/chatterino2)
- **Chatterino7**: [SevenTV/chatterino7](https://github.com/SevenTV/chatterino7)
- **Technorino**: [2547techno/technorino](https://github.com/2547techno/technorino)
- **Moltorino**: [molt/moltorino](https://github.com/molt/moltorino)

---

## 📄 License

This project is licensed under the [MIT License](LICENSE).
