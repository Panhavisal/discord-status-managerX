# Discord Status Manager

A lightweight Windows desktop app that automatically updates your Discord custom status based on what you're doing — coding, browsing, gaming, and more.

![Windows](https://img.shields.io/badge/platform-Windows-blue)
![C++17](https://img.shields.io/badge/language-C%2B%2B17-blue)
![WebView2](https://img.shields.io/badge/runtime-WebView2-green)

## Features

- **Automatic status detection** — Monitors running processes and sets your Discord status to match (e.g., "Coding in VS Code", "Browsing the web", "Playing Valorant")
- **Discord login via browser** — Embedded WebView2 window lets you log in to Discord directly; your credentials never touch the app
- **Three-tier token acquisition** — Auto-extract from Discord's local storage → WebView2 browser login → Manual entry fallback
- **System tray integration** — Runs quietly in the tray with a right-click menu:
  - **Status** — Shows current activity (e.g., "Connected - Coding in VS Code")
  - **Re-login** — Switch accounts or re-authenticate
  - **Logout** — Clear your token
  - **Edit Config** — Open `config.json` in your default editor
  - **Quit** — Exit the app
- **50+ app detections** — VS Code, Cursor, Visual Studio, IntelliJ IDEA, WebStorm, PyCharm, Chrome, Firefox, Steam, Spotify, and many more
- **Fully configurable** — Edit `config.json` to add, remove, or change app mappings and status text

## Screenshots

> *Coming soon*

## Download

Grab the latest release from [Releases](https://github.com/Panhavisal/discord-status-manager/releases).

You need **3 files** in the same folder:
- `DiscordStatusUpdater.exe`
- `WebView2Loader.dll`
- `config.json`

## Requirements

- **Windows 10** (version 1803+) or **Windows 11**
- **Microsoft Edge WebView2 Runtime** — Pre-installed on Windows 11; on Windows 10, [download it here](https://developer.microsoft.com/en-us/microsoft-edge/webview2/). If not installed, the app falls back to manual token entry.

## How It Works

1. **Token acquisition** — On first launch, the app tries three methods to get your Discord user token:
   - **Auto-extract** — Scans Discord's local storage for saved tokens
   - **WebView2 login** — Opens a Discord login page; after you log in, captures the token from browser network requests
   - **Manual entry** — Fallback dialog with instructions to get your token from browser DevTools
2. **Process monitoring** — Every 5 seconds, checks what apps are running
3. **Status update** — When a matched app is found, sends your custom status to Discord via the API
4. **Idle state** — When no matched app is running, sets your status to "Idle"

## Configuration

Edit `config.json` to customize which apps trigger which status:

```json
{
  "poll_interval_ms": 5000,
  "update_interval_s": 30,
  "activities": {
    "code.exe": { "text": "Coding in VS Code", "emoji_name": "" },
    "chrome.exe": { "text": "Browsing the web", "emoji_name": "" },
    "steam.exe": { "text": "Gaming on Steam", "emoji_name": "" }
  },
  "default_activity": {
    "text": "Idle",
    "emoji_name": ""
  }
}
```

- **`poll_interval_ms`** — How often to check running processes (default: 5000ms)
- **`update_interval_s`** — Minimum seconds between Discord API updates (default: 30s)
- **`activities`** — Map of process names to status text. Key is the `.exe` name (case-insensitive)
- **`default_activity`** — Status shown when no matched app is running
- **`emoji_name`** — Discord emoji name (leave empty `""` for plain text)

## Building from Source

### Prerequisites

- Visual Studio 2019+ with C++ desktop development workload
- CMake 3.15+

### Build

```bash
git clone https://github.com/Panhavisal/discord-status-manager.git
cd discord-status-manager
cmake -B build -G "Visual Studio 17 2022" -A x64
cmake --build build --config Release
```

The output will be in `build/Release/`.

## Project Structure

```
src/
├── main.cpp              # Entry point, token acquisition flow
├── config.cpp/h          # JSON config loader
├── discord_api.cpp/h     # Discord API client (WinINet)
├── token_extractor.cpp/h # LevelDB token extraction, DPAPI, AES-GCM
├── webview_login.cpp/h   # WebView2 Discord login dialog
├── setup_dialog.cpp/h    # Manual token entry dialog
├── worker.cpp/h          # Background process polling & status updates
├── process_monitor.cpp/h # Running process enumeration
├── tray_app.cpp/h        # System tray icon & menu
└── single_instance.cpp/h # Prevent multiple instances
```

## Privacy

- Your Discord credentials are **never** seen by this app. The WebView2 login window loads Discord's official website — you enter your password directly into Discord.
- The app only captures the `Authorization` header from browser network requests after you log in. This is the same token that Discord stores in your browser session.
- The token is held in memory only and is **never written to disk** by this app.
- All API calls go directly to `discord.com` over HTTPS.

## License

MIT