#pragma once

#include <windows.h>
#include <string>

// Manages Windows Service installation, uninstallation, and execution.
// When the app is run with --service, it enters service mode (no GUI, no tray).
// When run with --install / --uninstall, it manages the Windows Service entry.
class ServiceManager {
public:
    // Install the service with auto-start (requires elevation).
    // Returns true on success (including if service already exists).
    static bool Install();

    // Uninstall (stop first, then delete) the service (requires elevation).
    static bool Uninstall();

    // Entry point when --service is specified.
    // Calls StartServiceCtrlDispatcher, which calls our ServiceMain.
    static void RunAsService();

    // Log a message to both OutputDebugString and the Windows Event Log.
    static void Log(const std::string& message);

    // Service name constants
    static constexpr const char*  kServiceName        = "DiscordPresenceUpdater";
    static constexpr const wchar_t* kServiceNameW      = L"DiscordPresenceUpdater";
    static constexpr const wchar_t* kServiceDisplayName = L"Discord Presence Updater";

private:
    // Called by SCM via StartServiceCtrlDispatcher
    static void WINAPI ServiceMain(DWORD argc, LPWSTR* argv);

    // Control handler registered with RegisterServiceCtrlHandlerEx
    static DWORD WINAPI ServiceControlHandler(
        DWORD ctrl_code, DWORD evt_type,
        LPVOID evt_data, LPVOID context);

    // Report current service status to the SCM
    static void ReportStatus(DWORD current_state,
                             DWORD win32_exit_code = NO_ERROR,
                             DWORD wait_hint = 0);

    // The actual service work: load config/token, start worker, wait for stop
    static void ServiceWorker();

    // State
    static SERVICE_STATUS_HANDLE s_status_handle;
    static SERVICE_STATUS        s_status;
    static HANDLE                s_stop_event;
};