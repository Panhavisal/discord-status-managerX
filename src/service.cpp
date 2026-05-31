#include "service.h"
#include "token_store.h"
#include "worker.h"
#include "config.h"
#include "token_extractor.h"

#include <cstdio>

// ERROR_SERVICE_MARKED_DELETE = 0x432 (1074) — may not be defined with older SDK headers
#ifndef ERROR_SERVICE_MARKED_DELETE
#define ERROR_SERVICE_MARKED_DELETE 1074
#endif

// ---------------------------------------------------------------------------
// Static member definitions
// ---------------------------------------------------------------------------

SERVICE_STATUS_HANDLE ServiceManager::s_status_handle = nullptr;
SERVICE_STATUS        ServiceManager::s_status = {};
HANDLE                ServiceManager::s_stop_event = nullptr;

// ---------------------------------------------------------------------------
// Install / Uninstall
// ---------------------------------------------------------------------------

bool ServiceManager::Install() {
    // Get the current executable path
    wchar_t exe_path[MAX_PATH] = {};
    GetModuleFileNameW(nullptr, exe_path, MAX_PATH);

    // Build binary path with --service flag
    std::wstring binary_path = L"\"" + std::wstring(exe_path) + L"\" --service";

    SC_HANDLE h_sc = OpenSCManagerW(nullptr, nullptr, SC_MANAGER_CREATE_SERVICE);
    if (!h_sc) {
        DWORD err = GetLastError();
        wchar_t msg[512];
        swprintf_s(msg, L"Failed to open Service Control Manager.\n\nError: %lu\n\nMake sure you are running as Administrator.", err);
        MessageBoxW(nullptr, msg, L"Install Service — Error", MB_OK | MB_ICONERROR);
        return false;
    }

    SC_HANDLE h_svc = CreateServiceW(
        h_sc,
        kServiceNameW,                           // Service name (internal)
        kServiceDisplayName,                     // Display name (in services.msc)
        SERVICE_ALL_ACCESS,                       // Desired access
        SERVICE_WIN32_OWN_PROCESS,               // Own process
        SERVICE_AUTO_START,                       // Auto-start on boot
        SERVICE_ERROR_NORMAL,                     // Error control
        binary_path.c_str(),                      // Binary path with --service flag
        nullptr, nullptr, nullptr,               // Load order group, tag, dependencies
        nullptr,                                  // LocalSystem account
        nullptr                                   // No password
    );

    if (!h_svc) {
        DWORD err = GetLastError();
        CloseServiceHandle(h_sc);

        if (err == ERROR_SERVICE_EXISTS) {
            MessageBoxW(nullptr, L"Service already exists.", L"Install Service", MB_OK | MB_ICONINFORMATION);
            return true; // Not an error
        }

        wchar_t msg[512];
        swprintf_s(msg, L"Failed to create service.\n\nError: %lu", err);
        MessageBoxW(nullptr, msg, L"Install Service — Error", MB_OK | MB_ICONERROR);
        return false;
    }

    // Set a human-readable description
    SERVICE_DESCRIPTIONW desc;
    desc.lpDescription =
        L"Monitors running applications and updates your Discord custom status accordingly.";
    ChangeServiceConfig2W(h_svc, SERVICE_CONFIG_DESCRIPTION, &desc);

    std::wstring msg = L"Service installed successfully!\n\n"
        L"Name:    " + std::wstring(kServiceDisplayName) + L"\n"
        L"Binary:  " + binary_path + L"\n"
        L"Account: LocalSystem\n"
        L"Start:   Automatic\n\n"
        L"Use 'sc start DiscordPresenceUpdater' to start.";
    MessageBoxW(nullptr, msg.c_str(), L"Install Service", MB_OK | MB_ICONINFORMATION);

    CloseServiceHandle(h_svc);
    CloseServiceHandle(h_sc);
    return true;
}

bool ServiceManager::Uninstall() {
    SC_HANDLE h_sc = OpenSCManagerW(nullptr, nullptr, SC_MANAGER_CONNECT);
    if (!h_sc) {
        DWORD err = GetLastError();
        wchar_t msg[512];
        swprintf_s(msg, L"Failed to open Service Control Manager.\n\nError: %lu\n\nMake sure you are running as Administrator.", err);
        MessageBoxW(nullptr, msg, L"Uninstall Service — Error", MB_OK | MB_ICONERROR);
        return false;
    }

    SC_HANDLE h_svc = OpenServiceW(h_sc, kServiceNameW,
                                    SERVICE_STOP | DELETE | SERVICE_QUERY_STATUS);
    if (!h_svc) {
        DWORD err = GetLastError();
        wchar_t msg[512];
        swprintf_s(msg, L"Service not found.\n\nError: %lu", err);
        MessageBoxW(nullptr, msg, L"Uninstall Service", MB_OK | MB_ICONWARNING);
        CloseServiceHandle(h_sc);
        return false;
    }

    // Try to stop the service first
    SERVICE_STATUS status = {};
    if (ControlService(h_svc, SERVICE_CONTROL_STOP, &status)) {
        // Wait for the service to stop (up to 10 seconds)
        for (int i = 0; i < 10; ++i) {
            if (!QueryServiceStatus(h_svc, &status)) break;
            if (status.dwCurrentState == SERVICE_STOPPED) break;
            Sleep(1000);
        }
    }

    if (DeleteService(h_svc)) {
        MessageBoxW(nullptr, L"Service uninstalled successfully.", L"Uninstall Service", MB_OK | MB_ICONINFORMATION);
    } else {
        DWORD err = GetLastError();
        if (err == ERROR_SERVICE_MARKED_DELETE) {
            MessageBoxW(nullptr, L"Service marked for deletion.\nIt will be fully removed after stopping.", L"Uninstall Service", MB_OK | MB_ICONINFORMATION);
        } else {
            wchar_t msg[512];
            swprintf_s(msg, L"Failed to delete service.\n\nError: %lu", err);
            MessageBoxW(nullptr, msg, L"Uninstall Service — Error", MB_OK | MB_ICONERROR);
        }
    }

    CloseServiceHandle(h_svc);
    CloseServiceHandle(h_sc);
    return true;
}

// ---------------------------------------------------------------------------
// Run as Service
// ---------------------------------------------------------------------------

void ServiceManager::RunAsService() {
    SERVICE_TABLE_ENTRYW table[] = {
        { const_cast<LPWSTR>(kServiceNameW), ServiceMain },
        { nullptr, nullptr }
    };

    if (!StartServiceCtrlDispatcherW(table)) {
        Log("StartServiceCtrlDispatcher failed: " + std::to_string(GetLastError()));
    }
}

// ---------------------------------------------------------------------------
// ServiceMain — called by SCM on a thread pool thread
// ---------------------------------------------------------------------------

void WINAPI ServiceManager::ServiceMain(DWORD argc, LPWSTR* argv) {
    (void)argc; (void)argv;

    // Register the control handler
    s_status_handle = RegisterServiceCtrlHandlerExW(
        kServiceNameW, ServiceControlHandler, nullptr);
    if (!s_status_handle) return;

    // Initialize status
    ZeroMemory(&s_status, sizeof(s_status));
    s_status.dwServiceType      = SERVICE_WIN32_OWN_PROCESS;
    s_status.dwControlsAccepted = 0; // Will update when RUNNING
    s_status.dwWin32ExitCode    = NO_ERROR;

    // Create the stop event
    s_stop_event = CreateEventW(nullptr, TRUE, FALSE, nullptr);
    if (!s_stop_event) {
        ReportStatus(SERVICE_STOPPED, GetLastError());
        return;
    }

    ReportStatus(SERVICE_START_PENDING, NO_ERROR, 3000);

    // Do the work (blocks until stop event is signaled)
    ServiceWorker();

    // Cleanup
    CloseHandle(s_stop_event);
    s_stop_event = nullptr;
    ReportStatus(SERVICE_STOPPED);
}

// ---------------------------------------------------------------------------
// Service Control Handler
// ---------------------------------------------------------------------------

DWORD WINAPI ServiceManager::ServiceControlHandler(
    DWORD ctrl_code, DWORD evt_type, LPVOID evt_data, LPVOID context)
{
    (void)evt_type; (void)evt_data; (void)context;

    switch (ctrl_code) {
    case SERVICE_CONTROL_STOP:
    case SERVICE_CONTROL_PRESHUTDOWN:
        Log("Received stop signal.");
        ReportStatus(SERVICE_STOP_PENDING);
        if (s_stop_event) {
            SetEvent(s_stop_event);
        }
        return NO_ERROR;

    case SERVICE_CONTROL_INTERROGATE:
        return NO_ERROR;

    default:
        return ERROR_CALL_NOT_IMPLEMENTED;
    }
}

// ---------------------------------------------------------------------------
// ReportStatus — update SCM with current service state
// ---------------------------------------------------------------------------

void ServiceManager::ReportStatus(DWORD current_state,
                                   DWORD win32_exit_code, DWORD wait_hint) {
    s_status.dwCurrentState  = current_state;
    s_status.dwWin32ExitCode = win32_exit_code;
    s_status.dwWaitHint      = wait_hint;

    // Increment checkpoint for pending states
    if (current_state == SERVICE_RUNNING || current_state == SERVICE_STOPPED) {
        s_status.dwCheckPoint = 0;
    } else {
        s_status.dwCheckPoint++;
    }

    // Update accepted controls based on state
    if (current_state == SERVICE_RUNNING) {
        s_status.dwControlsAccepted = SERVICE_ACCEPT_STOP | SERVICE_ACCEPT_PRESHUTDOWN;
    } else {
        s_status.dwControlsAccepted = 0;
    }

    if (s_status_handle) {
        SetServiceStatus(s_status_handle, &s_status);
    }
}

// ---------------------------------------------------------------------------
// ServiceWorker — core service logic
// ---------------------------------------------------------------------------

void ServiceManager::ServiceWorker() {
    Log("Service starting...");

    // 1. Load configuration
    std::string config_path = Config::GetDefaultConfigPath();
    AppConfig config = Config::Load(config_path);

    // 2. Load saved token
    Log("Loading saved token...");
    std::string token = TokenStore::Load();
    if (token.empty()) {
        Log("ERROR: No token found in token.dat. Cannot start service. "
            "Run the application in GUI mode first to authenticate.");
        return; // Will report SERVICE_STOPPED with error
    }

    // 3. Validate the token
    Log("Validating token...");
    std::string username;
    if (!TokenExtractor::ValidateToken(token, username)) {
        Log("ERROR: Saved token is invalid or expired. Cannot start service. "
            "Run the application in GUI mode to re-authenticate.");
        return;
    }

    Log(std::string("Authenticated as: ") + username);
    ReportStatus(SERVICE_RUNNING);
    Log("Service is running.");

    // 4. Create and start the worker in service mode
    Worker worker(config, WorkerMode::Service);
    worker.SetToken(token);
    worker.SetLogCallback([](const std::string& msg) {
        ServiceManager::Log(msg);
    });
    worker.Start();

    // 5. Wait for SCM to tell us to stop
    WaitForSingleObject(s_stop_event, INFINITE);

    // 6. Graceful shutdown
    Log("Stopping worker...");
    worker.Stop();
    Log("Service stopped.");
}

// ---------------------------------------------------------------------------
// Log — OutputDebugString + Windows Event Log
// ---------------------------------------------------------------------------

void ServiceManager::Log(const std::string& message) {
    // Always log to debugger (visible in DebugView, Visual Studio, etc.)
    OutputDebugStringA(("DiscordPresence: " + message + "\n").c_str());

    // Log to Windows Event Log (visible in Event Viewer)
    HANDLE h_source = RegisterEventSourceA(nullptr, kServiceName);
    if (h_source) {
        const char* strings[] = { message.c_str() };
        ReportEventA(h_source, EVENTLOG_INFORMATION_TYPE, 0, 1,
                     nullptr, 1, 0, strings, nullptr);
        DeregisterEventSource(h_source);
    }
}