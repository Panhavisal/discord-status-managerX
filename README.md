# Discord Status Manager

A lightweight Windows desktop app that automatically updates your Discord custom status and Rich Presence based on what you're doing — coding, browsing, gaming, and more.

![Windows](https://img.shields.io/badge/platform-Windows-blue)
![C++17](https://img.shields.io/badge/language-C%2B%2B17-blue)
![WebView2](https://img.shields.io/badge/runtime-WebView2-green)

## Features

- **Automatic status detection** — Monitors running processes and sets your Discord status to match (e.g., "Coding in VS Code", "Browsing the web", "Playing Valorant")
- **Discord Rich Presence** — Shows a rich activity card on your profile (e.g., "Playing Visual Studio Code") with images, timestamps, and details — just like game activity
- **Discord login via browser** — Embedded WebView2 window lets you log in to Discord directly; your credentials never touch the app
- **Three-tier token acquisition** — Auto-extract from Discord's local storage → WebView2 browser login → Manual entry fallback
- **System tray integration** — Runs quietly in the tray with a right-click menu:
  - **Status** — Shows current activity (e.g., "Connected - Coding in VS Code")
  - **Re-login** — Switch accounts or re-authenticate
  - **Logout** — Clear your token
  - **Edit Config** — Open `config.json` in your default editor
  - **Quit** — Exit the app
- **140+ app detections** — VS Code, Cursor, Visual Studio, IntelliJ IDEA, WebStorm, PyCharm, Chrome, Firefox, Steam, Spotify, and many more
- **Windows Service support** — Install as an auto-start service with `--install`, runs headlessly on boot. See [SERVICE.md](SERVICE.md)
- **Encrypted token storage** — Your Discord token is encrypted at rest using Windows DPAPI (machine-bound, cannot be copied to another PC)
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
2. **Process monitoring** — Every 5 seconds, checks what apps are running (foreground window in GUI mode, all processes in service mode)
3. **Status update** — When a matched app is found, updates both:
   - **Custom Status** — Sends your text status to Discord via the HTTP API (requires token)
   - **Rich Presence** — Sends a rich activity card to Discord via the local IPC pipe (requires Application ID, no token needed)
4. **Idle state** — When no matched app is running, sets your status to the default activity

## Configuration

Edit `config.json` to customize which apps trigger which status:

```json
{
  "application_id": "",
  "enable_custom_status": true,
  "enable_rich_presence": true,
  "poll_interval_ms": 5000,
  "update_interval_s": 30,
  "activities": {
    "code.exe": {
      "text": "Coding in VS Code",
      "emoji_name": "",
      "rp_details": "Coding in VS Code",
      "rp_state": "Editing code",
      "rp_large_image": "vscode",
      "rp_large_text": "Visual Studio Code"
    },
    "chrome.exe": { "text": "Browsing the web", "emoji_name": "" },
    "steam.exe": { "text": "Gaming on Steam", "emoji_name": "" }
  },
  "default_activity": {
    "text": "Staying low...",
    "emoji_name": "🫥"
  }
}
```

### General Settings

- **`application_id`** — Discord Application ID for Rich Presence (see [Rich Presence Setup](#rich-presence-setup) below). Leave empty to disable Rich Presence.
- **`enable_custom_status`** — Update the text status line under your username via the HTTP API (default: `true`)
- **`enable_rich_presence`** — Update the Rich Presence activity card via Discord's local IPC (default: `true`, requires `application_id`)
- **`poll_interval_ms`** — How often to check running processes (default: 5000ms, minimum: 1000ms)
- **`update_interval_s`** — Minimum seconds between Discord API updates (default: 30s, minimum: 5s)
- **`activities`** — Map of process names to status text. Key is the `.exe` name (case-insensitive)
- **`default_activity`** — Status shown when no matched app is running
- **`emoji_name`** — Discord emoji name (leave empty `""` for plain text)

### Per-Activity Rich Presence Fields

Each activity entry can optionally include Rich Presence fields. If omitted, `rp_details` falls back to the `text` field, and other `rp_*` fields default to empty.

| Field | Description |
|-------|-------------|
| `rp_details` | First line of the Rich Presence card (e.g., "Coding in VS Code") |
| `rp_state` | Second line of the Rich Presence card (e.g., "Editing code") |
| `rp_large_image` | Key of a large image uploaded to your Discord Application, or a URL |
| `rp_large_text` | Hover text shown over the large image |
| `rp_small_image` | Key of a small image uploaded to your Discord Application, or a URL |
| `rp_small_text` | Hover text shown over the small image |

## Rich Presence Setup

Rich Presence shows a rich activity card on your Discord profile — the same style that games use to show "Playing Lunar Client" with images and elapsed time. It communicates with Discord via a local named pipe (no HTTP token needed).

### Step 1: Create a Discord Application

1. Go to [discord.com/developers/applications](https://discord.com/developers/applications)
2. Click **New Application**, give it a name (e.g., "Discord Status Manager")
3. Copy the **Application ID** from the General Information page
4. (Optional) Upload Rich Presence assets under **Rich Presence → Art Assets**

### Step 2: Configure

Paste the Application ID into `config.json`:

```json
{
  "application_id": "1234567890123456789",
  "enable_rich_presence": true,
  ...
}
```

### Step 3: Add Images (Optional)

Upload images in the Discord Developer Portal under **Rich Presence → Art Assets**. Then reference them by key name in your activity entries:

```json
"code.exe": {
  "text": "Coding in VS Code",
  "rp_details": "Coding in VS Code",
  "rp_large_image": "vscode",
  "rp_large_text": "Visual Studio Code"
}
```

### How It Works

| Feature | Custom Status | Rich Presence |
|---------|--------------|---------------|
| Shows as | Text + emoji under your name | Rich card with images and timestamps |
| API | HTTP `PATCH /users/@me/settings` | Local IPC via named pipe |
| Requires token | Yes | No |
| Requires app ID | No | Yes |
| Shows images | No | Yes |
| Shows elapsed time | No | Yes |
| Works without Discord desktop | Yes | No (requires running Discord client) |

Both can run simultaneously — set `enable_custom_status: true` and `enable_rich_presence: true` to update both at the same time. If the Discord desktop client isn't running, Rich Presence silently skips and Custom Status continues working.

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
├── main.cpp              # Entry point, CLI flags, token acquisition flow
├── config.cpp/h          # JSON config loader
├── discord_api.cpp/h     # Discord API client — Custom Status (WinINet)
├── discord_rpc.cpp/h    # Discord Rich Presence client — IPC named pipe
├── token_extractor.cpp/h # LevelDB token extraction, DPAPI, AES-GCM
├── token_store.cpp/h     # Token persistence (encrypted with DPAPI)
├── service.cpp/h         # Windows Service infrastructure (install/run/uninstall)
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
- The token is encrypted at rest using **Windows DPAPI** (Data Protection API) and stored in `token.dat`. The encrypted file is machine-bound — it cannot be decrypted on a different PC. Note: the encryption uses `CRYPTPROTECT_LOCAL_MACHINE` scope for compatibility with the Windows Service mode (which runs as LocalSystem), meaning any process on the same machine can decrypt it.
- All API calls go directly to `discord.com` over HTTPS.

## License

MIT