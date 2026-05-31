#include "config.h"
#include "tray_app.h"
#include "single_instance.h"
#include "setup_dialog.h"
#include "worker.h"
#include "token_extractor.h"
#include "webview_login.h"

#include <windows.h>
#include <ole2.h>

int WINAPI wWinMain(HINSTANCE hInstance, HINSTANCE, PWSTR, int) {
    // Single instance guard — prevent running twice
    SingleInstanceGuard guard(
        L"DiscordPresenceUpdater-{C4B1E2A0-3D5F-4A8B-9C7E-1F2A3B4C5D6E}");
    if (!guard.IsFirstInstance()) {
        MessageBoxW(nullptr, L"Another instance is already running.",
                    L"Discord Presence Updater", MB_OK | MB_ICONINFORMATION);
        return 1;
    }

    // Initialize COM for WebView2
    OleInitialize(nullptr);

    // Load configuration
    std::string config_path = Config::GetDefaultConfigPath();
    AppConfig config = Config::Load(config_path);

    // Try to auto-extract Discord token from local storage
    std::string token = TryAutoExtractToken(hInstance);

    // If auto-extraction failed, try WebView2 login
    if (token.empty()) {
        token = ShowWebView2LoginDialog(hInstance, nullptr);
    }

    // If WebView2 login also failed/unavailable, show manual entry dialog
    if (token.empty()) {
        token = ShowSetupDialog(hInstance, nullptr);
        if (token.empty()) {
            // User cancelled — exit
            OleUninitialize();
            return 0;
        }
    }

    // Create and start the worker thread
    Worker worker(config);
    worker.SetToken(token);
    worker.Start();

    // Create system tray application
    TrayApp tray(hInstance, "Discord Presence Updater");

    // Set quit callback: stop the worker when user clicks "Quit"
    tray.SetQuitCallback([&worker]() {
        worker.Stop();
    });

    // Set re-login callback: open login dialog and update token
    tray.SetReLoginCallback([&worker, hInstance]() -> std::string {
        // Stop the worker while we re-login
        worker.Stop();

        // Try WebView2 login first, then manual dialog
        std::string new_token = ShowWebView2LoginDialog(hInstance, nullptr);
        if (new_token.empty()) {
            new_token = ShowSetupDialog(hInstance, nullptr);
        }

        if (!new_token.empty()) {
            worker.SetToken(new_token);
            worker.Start();
        }
        // If cancelled, worker stays stopped (user logged out effectively)
        return new_token;
    });

    // Set logout callback: clear the token
    tray.SetLogoutCallback([&worker]() {
        worker.ClearToken();
    });

    // Set tooltip provider: periodically refresh tooltip with worker status
    tray.SetTooltipProvider([&worker]() -> std::string {
        return "Discord Presence - " + worker.GetStatusText();
    });

    // Pass config path for "Edit Config" menu item
    tray.SetConfigPath(config_path);

    if (!tray.Initialize()) {
        worker.Stop();
        OleUninitialize();
        return 1;
    }

    // Run message loop (blocks until quit)
    tray.Run();

    // Worker is stopped by quit callback; thread joins in destructor
    OleUninitialize();
    return 0;
}