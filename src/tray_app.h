#pragma once

#include <windows.h>
#include <shellapi.h>
#include <string>
#include <functional>
#include <mutex>
#include <list>

class TrayApp {
public:
    TrayApp(HINSTANCE h_instance, const std::string& tooltip);
    ~TrayApp();

    bool Initialize();
    void Run();
    void Quit();

    using QuitCallback = std::function<void()>;
    void SetQuitCallback(QuitCallback cb);

    // Callback that returns a new token (empty = cancelled/failed).
    // Called on the main thread; should open a login dialog.
    using ReLoginCallback = std::function<std::string()>;
    void SetReLoginCallback(ReLoginCallback cb);

    // Callback that clears the stored token.
    using LogoutCallback = std::function<void()>;
    void SetLogoutCallback(LogoutCallback cb);

    // Thread-safe: posts a message to update the tray tooltip
    void SetTooltip(const std::string& text);

    // Thread-safe: sets a callback to get tooltip text periodically
    using TooltipProvider = std::function<std::string()>;
    void SetTooltipProvider(TooltipProvider provider);

    // Set the config file path (for "Edit Config" menu item)
    void SetConfigPath(const std::string& path);

private:
    static LRESULT CALLBACK WndProcStatic(HWND hwnd, UINT msg,
                                           WPARAM wparam, LPARAM lparam);
    LRESULT WndProc(HWND hwnd, UINT msg, WPARAM wparam, LPARAM lparam);
    void ShowContextMenu(HWND hwnd);
    void AddTrayIcon();
    void RemoveTrayIcon();
    void DrainPendingTooltips();  // Clean up any unconsumed heap strings

    HINSTANCE       h_instance_;
    HWND            hwnd_ = nullptr;
    NOTIFYICONDATAW nid_ = {};
    HICON           h_icon_ = nullptr;
    std::wstring    tooltip_;
    QuitCallback    quit_callback_;
    ReLoginCallback relogin_callback_;
    LogoutCallback  logout_callback_;
    TooltipProvider tooltip_provider_;
    bool            running_ = false;
    std::string     config_path_;

    UINT taskbar_created_msg_ = 0;

    // Pending tooltip strings — protected by mutex.
    // Stored as shared_ptr so WM_TRAY_UPDATE handler can safely consume them.
    std::mutex                    pending_tooltip_mutex_;
    std::list<std::shared_ptr<std::string>> pending_tooltips_;

    static constexpr UINT WM_TRAYICON    = WM_APP + 1;
    static constexpr UINT WM_TRAY_UPDATE = WM_APP + 2;
    static constexpr int  ID_TRAY        = 1;
    static constexpr int  IDM_STATUS     = 2000;
    static constexpr int  IDM_RELOGIN    = 2001;
    static constexpr int  IDM_LOGOUT     = 2002;
    static constexpr int  IDM_EDIT_CONFIG = 2003;
    static constexpr int  IDM_QUIT       = 2004;
    static constexpr UINT TIMER_TOOLTIP  = 1;
    static constexpr UINT TOOLTIP_INTERVAL_MS = 2000;
};