#include "config.h"
#include "tray_app.h"
#include "single_instance.h"
#include "setup_dialog.h"
#include "worker.h"
#include "token_extractor.h"
#include "webview_login.h"
#include "token_store.h"
#include "service.h"

#include <windows.h>
#include <ole2.h>

// UTF-8 to wide string conversion (handles non-ASCII usernames correctly)
static std::wstring Utf8ToWide(const std::string& str) {
    if (str.empty()) return L"";
    int len = MultiByteToWideChar(CP_UTF8, 0, str.c_str(),
                                   static_cast<int>(str.size()), nullptr, 0);
    std::wstring result(len, 0);
    MultiByteToWideChar(CP_UTF8, 0, str.c_str(),
                        static_cast<int>(str.size()), &result[0], len);
    return result;
}

// ---------------------------------------------------------------------------
// Splash window: shows status briefly before going to tray
// ---------------------------------------------------------------------------

static const wchar_t SPLASH_CLASS[] = L"DiscordPresenceSplashWnd";

// Splash window state: tracks both the window and its font for leak-free cleanup
struct SplashState {
    HWND hwnd = nullptr;
    HFONT hFont = nullptr;
};

static LRESULT CALLBACK SplashWndProc(HWND hwnd, UINT msg,
                                       WPARAM wparam, LPARAM lparam) {
    return DefWindowProcW(hwnd, msg, wparam, lparam);
}

// Shows a small centered window with a status message for a few seconds.
// Returns the SplashState so the caller can update the text or close it.
static SplashState ShowSplash(HINSTANCE h_instance, const std::wstring& message) {
    SplashState state;

    static bool class_registered = false;
    if (!class_registered) {
        WNDCLASSEXW wc = {};
        wc.cbSize        = sizeof(wc);
        wc.lpfnWndProc   = SplashWndProc;
        wc.hInstance      = h_instance;
        wc.hCursor        = LoadCursor(nullptr, IDC_ARROW);
        wc.hbrBackground  = reinterpret_cast<HBRUSH>(COLOR_WINDOW + 1);
        wc.lpszClassName  = SPLASH_CLASS;
        RegisterClassExW(&wc);
        class_registered = true;
    }

    constexpr int WIDTH = 360;
    constexpr int HEIGHT = 100;

    HWND hwnd = CreateWindowExW(
        WS_EX_TOOLWINDOW | WS_EX_APPWINDOW,
        SPLASH_CLASS,
        L"Discord Presence Updater",
        WS_OVERLAPPED | WS_CAPTION,
        CW_USEDEFAULT, CW_USEDEFAULT,
        WIDTH, HEIGHT,
        nullptr, nullptr, h_instance, nullptr
    );
    if (!hwnd) return state;

    // Center on screen
    RECT rc;
    GetWindowRect(hwnd, &rc);
    SetWindowPos(hwnd, nullptr,
        (GetSystemMetrics(SM_CXSCREEN) - (rc.right - rc.left)) / 2,
        (GetSystemMetrics(SM_CYSCREEN) - (rc.bottom - rc.top)) / 2,
        0, 0, SWP_NOZORDER | SWP_NOSIZE);

    HFONT hFont = CreateFontW(
        -14, 0, 0, 0, FW_NORMAL, FALSE, FALSE, FALSE,
        DEFAULT_CHARSET, OUT_DEFAULT_PRECIS, CLIP_DEFAULT_PRECIS,
        DEFAULT_QUALITY, DEFAULT_PITCH | FF_DONTCARE, L"Segoe UI");

    HWND hLabel = CreateWindowExW(0, L"STATIC", L"",
        WS_CHILD | WS_VISIBLE | SS_CENTER,
        10, 25, WIDTH - 40, 40,
        hwnd, nullptr, h_instance, nullptr);

    if (!hLabel) {
        // Label creation failed — clean up font immediately
        DeleteObject(hFont);
        DestroyWindow(hwnd);
        return state;
    }

    SendMessageW(hLabel, WM_SETFONT, reinterpret_cast<WPARAM>(hFont), TRUE);
    SetWindowTextW(hLabel, message.c_str());

    ShowWindow(hwnd, SW_SHOW);
    UpdateWindow(hwnd);

    // Process messages so the window actually paints
    MSG msg;
    while (PeekMessageW(&msg, hwnd, 0, 0, PM_REMOVE)) {
        TranslateMessage(&msg);
        DispatchMessageW(&msg);
    }

    state.hwnd = hwnd;
    state.hFont = hFont;
    return state;
}

// Update splash text
static void UpdateSplash(const SplashState& state, const std::wstring& message) {
    if (!state.hwnd) return;
    HWND hLabel = FindWindowExW(state.hwnd, nullptr, L"STATIC", nullptr);
    if (hLabel) {
        SetWindowTextW(hLabel, message.c_str());
    }
    // Process messages so the update paints
    MSG msg;
    while (PeekMessageW(&msg, state.hwnd, 0, 0, PM_REMOVE)) {
        TranslateMessage(&msg);
        DispatchMessageW(&msg);
    }
}

// Close and clean up splash
static void CloseSplash(SplashState& state) {
    if (!state.hwnd) return;
    // Pump remaining messages briefly so the window finishes painting
    MSG msg;
    while (PeekMessageW(&msg, state.hwnd, 0, 0, PM_REMOVE)) {
        TranslateMessage(&msg);
        DispatchMessageW(&msg);
    }
    DestroyWindow(state.hwnd);
    state.hwnd = nullptr;
    // Unconditionally delete the font we created
    if (state.hFont) {
        DeleteObject(state.hFont);
        state.hFont = nullptr;
    }
}

// ---------------------------------------------------------------------------
// Entry point
// ---------------------------------------------------------------------------

int WINAPI wWinMain(HINSTANCE hInstance, HINSTANCE, PWSTR, int) {
    // --- Command-line argument handling ---
    int argc = 0;
    LPWSTR* argv = CommandLineToArgvW(GetCommandLineW(), &argc);
    if (!argv) {
        // CommandLineToArgvW failed — treat as no arguments
        argc = 0;
    }

    bool install   = false;
    bool uninstall = false;
    bool service   = false;

    for (int i = 1; i < argc; ++i) {
        std::wstring arg(argv[i]);
        if (arg == L"--install" || arg == L"-i")        install   = true;
        else if (arg == L"--uninstall" || arg == L"-u")  uninstall = true;
        else if (arg == L"--service" || arg == L"-s")    service   = true;
    }
    LocalFree(argv);

    if (install) {
        bool ok = ServiceManager::Install();
        return ok ? 0 : 1;
    }

    if (uninstall) {
        bool ok = ServiceManager::Uninstall();
        return ok ? 0 : 1;
    }

    if (service) {
        ServiceManager::RunAsService();
        return 0;
    }

    // --- GUI mode (default) ---

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

    // Show splash window
    SplashState splash = ShowSplash(hInstance, L"Starting Discord Presence Updater...");

    // Try to load a previously saved token first
    std::string token = TokenStore::Load();

    if (!token.empty()) {
        // Validate the saved token
        UpdateSplash(splash, L"Validating saved token...");
        MSG msg;
        while (PeekMessageW(&msg, nullptr, 0, 0, PM_REMOVE)) {
            TranslateMessage(&msg);
            DispatchMessageW(&msg);
        }

        std::string username;
        if (TokenExtractor::ValidateToken(token, username)) {
            std::wstring status = L"Connected as " + Utf8ToWide(username);
            UpdateSplash(splash, status);
        } else {
            // Saved token is invalid — clear it and try other methods
            token.clear();
            TokenStore::Clear();
            UpdateSplash(splash, L"Token expired, re-authenticating...");
        }
    }

    // If no saved token, try auto-extraction from Discord local storage
    if (token.empty()) {
        CloseSplash(splash);
        token = TryAutoExtractToken(hInstance);
    }

    // If auto-extraction failed, try WebView2 login
    if (token.empty()) {
        token = ShowWebView2LoginDialog(hInstance, nullptr);
    }

    // If WebView2 login also failed/unavailable, show manual entry dialog
    if (token.empty()) {
        token = ShowSetupDialog(hInstance, nullptr);
        if (token.empty()) {
            // User cancelled — exit
            CloseSplash(splash);
            OleUninitialize();
            return 0;
        }
    }

    // Save the token for next launch
    TokenStore::Save(token);

    // Show connected splash (if not already showing)
    if (!splash.hwnd) {
        splash = ShowSplash(hInstance, L"Connected!");
    } else {
        UpdateSplash(splash, L"Connected!");
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
            TokenStore::Save(new_token);
            worker.SetToken(new_token);
            worker.Start();
        }
        // If cancelled, worker stays stopped (user logged out effectively)
        return new_token;
    });

    // Set logout callback: clear the token
    tray.SetLogoutCallback([&worker]() {
        worker.ClearToken();
        TokenStore::Clear();
    });

    // Set tooltip provider: periodically refresh tooltip with worker status
    tray.SetTooltipProvider([&worker]() -> std::string {
        return "Discord Presence - " + worker.GetStatusText();
    });

    // Pass config path for "Edit Config" menu item
    tray.SetConfigPath(config_path);

    if (!tray.Initialize()) {
        worker.Stop();
        CloseSplash(splash);
        OleUninitialize();
        return 1;
    }

    // Keep splash visible for a moment so user sees the "Connected" message
    // Use a message-pumping wait instead of Sleep() to keep the tray responsive
    DWORD splash_start = GetTickCount();
    while (GetTickCount() - splash_start < 1500) {
        MSG msg;
        while (PeekMessageW(&msg, nullptr, 0, 0, PM_REMOVE)) {
            TranslateMessage(&msg);
            DispatchMessageW(&msg);
        }
        Sleep(50);
    }
    CloseSplash(splash);

    // Clean up the splash window class (no longer needed)
    UnregisterClassW(SPLASH_CLASS, hInstance);

    // Run message loop (blocks until quit)
    tray.Run();

    // Worker is stopped by quit callback; thread joins in destructor
    OleUninitialize();
    return 0;
}