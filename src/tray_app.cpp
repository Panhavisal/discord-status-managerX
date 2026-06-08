#include "tray_app.h"
#include "resource.h"

#include <shellapi.h>
#include <memory>

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
// Construction / Destruction
// ---------------------------------------------------------------------------

TrayApp::TrayApp(HINSTANCE h_instance, const std::string& tooltip)
    : h_instance_(h_instance), tooltip_(Utf8ToWide(tooltip)) {}

TrayApp::~TrayApp() {
    RemoveTrayIcon();
    if (hwnd_) {
        // DestroyWindow processes remaining WM_TRAY_UPDATE messages,
        // which consume entries from pending_tooltips_. After this,
        // no more WM_TRAY_UPDATE messages can arrive (window is gone),
        // so it's safe to drain the remaining shared_ptrs.
        DestroyWindow(hwnd_);
        hwnd_ = nullptr;
    }
    DrainPendingTooltips();
    // Note: h_icon_ was loaded with LR_SHARED, so DestroyIcon is not needed.
    // Shared icons are managed by the system and are valid until the module is unloaded.
    UnregisterClassW(L"DiscordPresenceTrayWnd", h_instance_);
}

// ---------------------------------------------------------------------------
// Initialization
// ---------------------------------------------------------------------------

bool TrayApp::Initialize() {
    taskbar_created_msg_ = RegisterWindowMessageW(L"TaskbarCreated");

    // Register window class
    WNDCLASSEXW wc = {};
    wc.cbSize        = sizeof(wc);
    wc.lpfnWndProc   = WndProcStatic;
    wc.hInstance      = h_instance_;
    wc.lpszClassName  = L"DiscordPresenceTrayWnd";
    if (!RegisterClassExW(&wc)) {
        DWORD err = GetLastError();
        if (err != ERROR_CLASS_ALREADY_EXISTS) {
            return false;
        }
    }

    // Create hidden window
    hwnd_ = CreateWindowExW(
        0,
        L"DiscordPresenceTrayWnd",
        L"Discord Presence Updater",
        0,  // no visible style
        CW_USEDEFAULT, CW_USEDEFAULT,
        CW_USEDEFAULT, CW_USEDEFAULT,
        HWND_MESSAGE, // message-only window (not visible, no taskbar)
        nullptr,
        h_instance_,
        this // pass 'this' via lpParam
    );
    if (!hwnd_) return false;

    // Load icon from resources
    h_icon_ = static_cast<HICON>(
        LoadImage(h_instance_, MAKEINTRESOURCE(IDI_APP_ICON),
                  IMAGE_ICON, 0, 0, LR_DEFAULTSIZE | LR_SHARED)
    );

    AddTrayIcon();

    // Start tooltip refresh timer
    SetTimer(hwnd_, TIMER_TOOLTIP, TOOLTIP_INTERVAL_MS, nullptr);

    return true;
}

// ---------------------------------------------------------------------------
// Message Loop
// ---------------------------------------------------------------------------

void TrayApp::Run() {
    running_ = true;
    MSG msg;
    while (GetMessageW(&msg, nullptr, 0, 0)) {
        TranslateMessage(&msg);
        DispatchMessageW(&msg);
    }
    running_ = false;
}

void TrayApp::Quit() {
    if (hwnd_) {
        PostMessageW(hwnd_, WM_CLOSE, 0, 0);
    }
}

// ---------------------------------------------------------------------------
// Callbacks
// ---------------------------------------------------------------------------

void TrayApp::SetQuitCallback(QuitCallback cb) {
    quit_callback_ = std::move(cb);
}

void TrayApp::SetReLoginCallback(ReLoginCallback cb) {
    relogin_callback_ = std::move(cb);
}

void TrayApp::SetLogoutCallback(LogoutCallback cb) {
    logout_callback_ = std::move(cb);
}

void TrayApp::SetTooltip(const std::string& text) {
    if (hwnd_) {
        auto ptr = std::make_shared<std::string>(text);
        {
            std::lock_guard<std::mutex> lock(pending_tooltip_mutex_);
            pending_tooltips_.push_back(ptr);
        }
        PostMessageW(hwnd_, WM_TRAY_UPDATE, 0,
                      reinterpret_cast<LPARAM>(ptr.get()));
    }
}

void TrayApp::SetTooltipProvider(TooltipProvider provider) {
    tooltip_provider_ = std::move(provider);
}

void TrayApp::SetConfigPath(const std::string& path) {
    config_path_ = path;
}

// ---------------------------------------------------------------------------
// Tray Icon
// ---------------------------------------------------------------------------

void TrayApp::AddTrayIcon() {
    ZeroMemory(&nid_, sizeof(nid_));
    nid_.cbSize           = sizeof(nid_);
    nid_.hWnd             = hwnd_;
    nid_.uID              = ID_TRAY;
    nid_.uFlags           = NIF_ICON | NIF_TIP | NIF_MESSAGE;
    nid_.uCallbackMessage = WM_TRAYICON;
    nid_.hIcon            = h_icon_ ? h_icon_ :
                            LoadIcon(nullptr, IDI_APPLICATION);

    // Set tooltip (126 chars max to avoid splitting a UTF-16 surrogate pair)
    std::wstring tip = tooltip_.substr(0, 126);
    wcscpy_s(nid_.szTip, tip.c_str());

    Shell_NotifyIconW(NIM_ADD, &nid_);

    // Set NOTIFYICON_VERSION_4 for modern callback semantics
    nid_.uVersion = NOTIFYICON_VERSION_4;
    Shell_NotifyIconW(NIM_SETVERSION, &nid_);
}

void TrayApp::RemoveTrayIcon() {
    if (nid_.hWnd) {
        Shell_NotifyIconW(NIM_DELETE, &nid_);
        nid_.hWnd = nullptr;
    }
}

// ---------------------------------------------------------------------------
// Context Menu
// ---------------------------------------------------------------------------

void TrayApp::ShowContextMenu(HWND hwnd) {
    POINT pt;
    GetCursorPos(&pt);
    HMENU hMenu = CreatePopupMenu();

    // Status line (disabled — shows current activity)
    std::wstring status_text = tooltip_;
    if (status_text.length() > 63) {
        status_text = status_text.substr(0, 60) + L"...";
    }
    AppendMenuW(hMenu, MF_STRING | MF_DISABLED | MF_GRAYED,
                IDM_STATUS, status_text.c_str());

    // Separator
    AppendMenuW(hMenu, MF_SEPARATOR, 0, nullptr);

    // Actions
    AppendMenuW(hMenu, MF_STRING, IDM_RELOGIN,    L"Re-login...");
    AppendMenuW(hMenu, MF_STRING, IDM_LOGOUT,     L"Logout");
    AppendMenuW(hMenu, MF_STRING, IDM_EDIT_CONFIG, L"Edit Config");

    // Separator
    AppendMenuW(hMenu, MF_SEPARATOR, 0, nullptr);

    // Quit
    AppendMenuW(hMenu, MF_STRING, IDM_QUIT, L"Quit");

    // Required for menu to dismiss properly
    SetForegroundWindow(hwnd);

    TrackPopupMenu(hMenu, TPM_BOTTOMALIGN | TPM_LEFTALIGN,
                   pt.x, pt.y, 0, hwnd, nullptr);

    // Force message processing so menu dismisses
    PostMessageW(hwnd, WM_NULL, 0, 0);
    DestroyMenu(hMenu);
}

// ---------------------------------------------------------------------------
// Window Procedure
// ---------------------------------------------------------------------------

LRESULT CALLBACK TrayApp::WndProcStatic(HWND hwnd, UINT msg,
                                         WPARAM wparam, LPARAM lparam) {
    TrayApp* self = nullptr;

    if (msg == WM_NCCREATE) {
        auto cs = reinterpret_cast<CREATESTRUCT*>(lparam);
        self = reinterpret_cast<TrayApp*>(cs->lpCreateParams);
        SetWindowLongPtrW(hwnd, GWLP_USERDATA, reinterpret_cast<LONG_PTR>(self));
    } else {
        self = reinterpret_cast<TrayApp*>(
            GetWindowLongPtrW(hwnd, GWLP_USERDATA));
    }

    if (self) {
        return self->WndProc(hwnd, msg, wparam, lparam);
    }

    return DefWindowProcW(hwnd, msg, wparam, lparam);
}

LRESULT TrayApp::WndProc(HWND hwnd, UINT msg, WPARAM wparam, LPARAM lparam) {
    // TaskbarCreated — re-add icon if Explorer restarts
    if (msg == taskbar_created_msg_) {
        AddTrayIcon();
        return 0;
    }

    switch (msg) {
    case WM_TRAYICON: {
        switch (LOWORD(lparam)) {
        case WM_CONTEXTMENU:
        case WM_RBUTTONUP:
            ShowContextMenu(hwnd);
            break;
        default:
            break;
        }
        return 0;
    }

    case WM_TRAY_UPDATE: {
        // Update tooltip from pending list
        auto* text_ptr = reinterpret_cast<std::string*>(lparam);
        if (text_ptr) {
            std::wstring wide = Utf8ToWide(*text_ptr);
            wide = wide.substr(0, 126);
            tooltip_ = wide;
            wcscpy_s(nid_.szTip, wide.c_str());
            Shell_NotifyIconW(NIM_MODIFY, &nid_);

            // Remove consumed entry from the tracked list
            std::lock_guard<std::mutex> lock(pending_tooltip_mutex_);
            pending_tooltips_.remove_if([text_ptr](const std::shared_ptr<std::string>& p) {
                return p.get() == text_ptr;
            });
        }
        return 0;
    }

    case WM_COMMAND: {
        switch (LOWORD(wparam)) {
        case IDM_RELOGIN:
            if (relogin_callback_) {
                std::string new_token = relogin_callback_();
                // Token is applied by the callback; nothing else to do here
                (void)new_token;
            }
            break;

        case IDM_LOGOUT:
            if (logout_callback_) {
                logout_callback_();
            }
            break;

        case IDM_EDIT_CONFIG:
            if (!config_path_.empty()) {
                ShellExecuteW(nullptr, L"open",
                    Utf8ToWide(config_path_).c_str(),
                    nullptr, nullptr, SW_SHOWNORMAL);
            }
            break;

        case IDM_QUIT:
            if (quit_callback_) quit_callback_();
            if (hwnd_) {
                DestroyWindow(hwnd_);
            } else {
                PostQuitMessage(0);
            }
            break;

        default:
            break;
        }
        return 0;
    }

    case WM_TIMER: {
        if (wparam == TIMER_TOOLTIP && tooltip_provider_) {
            std::string text = tooltip_provider_();
            std::wstring wide = Utf8ToWide(text);
            wide = wide.substr(0, 126);
            if (wide != tooltip_) {
                tooltip_ = wide;
                wcscpy_s(nid_.szTip, wide.c_str());
                Shell_NotifyIconW(NIM_MODIFY, &nid_);
            }
        }
        return 0;
    }

    case WM_DESTROY:
        KillTimer(hwnd, TIMER_TOOLTIP);
        RemoveTrayIcon();
        hwnd_ = nullptr;
        PostQuitMessage(0);
        return 0;

    default:
        return DefWindowProcW(hwnd, msg, wparam, lparam);
    }
}

// ---------------------------------------------------------------------------
// Cleanup
// ---------------------------------------------------------------------------

void TrayApp::DrainPendingTooltips() {
    std::lock_guard<std::mutex> lock(pending_tooltip_mutex_);
    pending_tooltips_.clear();
}