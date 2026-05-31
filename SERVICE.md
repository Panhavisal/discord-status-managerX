# Discord Status Manager — Windows Service Guide

The Discord Status Manager can run as a **Windows Service** that auto-starts on boot, so your Discord status updates even when you're not logged in.

> **Prerequisites:** You must run the app in **GUI mode at least once** to authenticate and save your Discord token before the service can work. The service cannot show login dialogs — it reads the saved encrypted token from `token.dat`.

---

## Quick Start

```powershell
# 1. Run the app once in GUI mode to authenticate
DiscordStatusUpdater.exe

# 2. Install the service (requires Administrator)
DiscordStatusUpdater.exe --install

# 3. Start the service
sc start DiscordPresenceUpdater
```

That's it. Your Discord status will now update automatically on every boot.

---

## CLI Flags

| Flag | Shorthand | Description |
|------|-----------|-------------|
| `--install` | `-i` | Install the service (requires admin) |
| `--uninstall` | `-u` | Uninstall the service (requires admin) |
| `--service` | `-s` | Run as a service (called by SCM, not manually) |
| *(none)* | — | Run in GUI mode (default, tray icon) |

---

## Step-by-Step

### 1. Authenticate First

Run the app normally (double-click or run without flags). Log in via the WebView2 window or manual entry. The app saves your token to `token.dat` (encrypted with DPAPI — tied to this machine).

```
DiscordStatusUpdater.exe
```

Once you see "Connected" in the tray, you can close the app. The token is saved.

### 2. Install the Service

Open an **elevated** Command Prompt or PowerShell (right-click → Run as Administrator):

```powershell
DiscordStatusUpdater.exe --install
```

Output:
```
Service installed successfully.
  Name:    Discord Presence Updater
  Binary:  "C:\path\to\DiscordStatusUpdater.exe" --service
  Account: LocalSystem
  Start:   Automatic
```

The service is configured for **automatic startup** — it will run on every boot.

### 3. Start the Service

```powershell
sc start DiscordPresenceUpdater
```

Or alternatively:
```powershell
net start DiscordPresenceUpdater
```

### 4. Check Service Status

```powershell
sc query DiscordPresenceUpdater
```

You should see:
```
STATE              : 4  RUNNING
```

### 5. View Logs

The service logs to the **Windows Event Log** under the source `DiscordPresenceUpdater`:

```powershell
# PowerShell — view recent logs
Get-EventLog -LogName Application -Source DiscordPresenceUpdater -Newest 10
```

Or open **Event Viewer** (`eventvwr.msc`) → Windows Logs → Application → filter by source `DiscordPresenceUpdater`.

Logs are also written to `OutputDebugString` (viewable with [DebugView](https://learn.microsoft.com/en-us/sysinternals/downloads/debugview)).

---

## Stopping & Uninstalling

### Stop the Service

```powershell
sc stop DiscordPresenceUpdater
```

### Uninstall the Service

```powershell
DiscordStatusUpdater.exe --uninstall
```

This stops the service (if running) and removes it from the Service Control Manager.

---

## How Service Mode Works

| | GUI Mode | Service Mode |
|---|---|---|
| **How to start** | Double-click the exe | `sc start` or auto-start on boot |
| **Token acquisition** | Auto-extract → WebView2 → Manual | Reads encrypted `token.dat` only |
| **If token is invalid** | Shows re-login dialog | Logs error and stops the service |
| **Process detection** | Foreground (focused) window | All running processes |
| **UI** | Splash screen, tray icon, menus | None (headless) |
| **Logging** | Tray tooltip | Windows Event Log + OutputDebugString |
| **Shutdown** | Tray menu → Quit | `sc stop` or system shutdown |

### Process Detection Difference

In **GUI mode**, the app detects the *foreground* (focused) window — if VS Code is running but minimized while you browse Chrome, your status shows "Browsing the web".

In **service mode** (running in Session 0 with no desktop), foreground detection doesn't work. Instead, it scans **all running processes** — if VS Code is running at all (even minimized), your status shows "Coding in VS Code".

---

## Troubleshooting

### Service fails to start

**Check the Event Log:**
```powershell
Get-EventLog -LogName Application -Source DiscordPresenceUpdater -Newest 5
```

Common errors:

| Error | Cause | Fix |
|-------|-------|-----|
| `No token found in token.dat` | Token was never saved | Run the app in GUI mode first to authenticate |
| `Saved token is invalid or expired` | Token expired or was revoked | Run in GUI mode to re-authenticate, then restart the service |
| `StartServiceCtrlDispatcher failed` | `--service` flag was not in the binary path | Reinstall: `DiscordStatusUpdater.exe --uninstall` then `--install` |

### Service starts but status doesn't update

- Make sure `config.json` is next to the exe. The service reads it from the same directory.
- Check the Event Log for `Discord API error` or `Token expired or unauthorized`.
- If the token expired, run the app in GUI mode to re-authenticate, then restart the service.

### Can't install — "Access denied"

You must run the install/uninstall commands from an **elevated** (Administrator) terminal. Right-click Command Prompt → Run as Administrator.

### Token works in GUI mode but not in service mode

The token file is encrypted with DPAPI using `CRYPTPROTECT_LOCAL_MACHINE`, which means it's accessible from any local account including LocalSystem. If you have an old `token.dat` that was encrypted with user-level DPAPI, delete it and re-authenticate:

```powershell
del token.dat
DiscordStatusUpdater.exe       # Re-authenticate in GUI mode
sc start DiscordPresenceUpdater
```

---

## Architecture

```
┌──────────────────────────────────────────────┐
│            Service Control Manager            │
│         (starts on boot, manages lifecycle)  │
└──────────────────┬───────────────────────────┘
                   │ starts with --service flag
┌──────────────────▼───────────────────────────┐
│              ServiceMain                      │
│  ┌─────────────────────────────────────────┐ │
│  │  1. Load config.json                     │ │
│  │  2. Load & validate token from token.dat │ │
│  │  3. Start Worker (background thread)     │ │
│  │  4. Wait for SCM stop signal            │ │
│  │  5. Stop worker, clean up               │ │
│  └─────────────────────────────────────────┘ │
└──────────────────────────────────────────────┘
```

The service uses the same `Worker` class as the GUI mode, but with `WorkerMode::Service` which switches process detection from foreground-window to all-running-processes scanning.