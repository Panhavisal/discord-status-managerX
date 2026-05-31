#include "webview_login.h"

// WebView2.h requires COM interface definitions (IUnknown, etc.)
// that WIN32_LEAN_AND_MEAN excludes. Include objbase.h explicitly.
#include <objbase.h>
#include <WebView2.h>
#include <wrl/client.h>
#include <wrl/event.h>
#include <shlobj.h>

#include <string>
#include <algorithm>

using namespace Microsoft::WRL;

// ---------------------------------------------------------------------------
// Constants
// ---------------------------------------------------------------------------

static const wchar_t LOGIN_CLASS[] = L"DiscordPresenceWebViewLogin";
static constexpr int DLG_WIDTH   = 800;
static constexpr int DLG_HEIGHT  = 640;
static constexpr int BANNER_HEIGHT = 50;

// Custom message: token was captured from an API request
static constexpr UINT WM_TOKEN_CAPTURED = WM_APP + 100;

// Control IDs
static constexpr int ID_LABEL_BANNER = 1001;

// ---------------------------------------------------------------------------
// State passed between ShowWebView2LoginDialog and the WndProc
// ---------------------------------------------------------------------------

struct WebViewLoginState {
    std::string token;
    bool        confirmed       = false;
    bool        creation_failed = false;
    bool        webview_ready   = false;
    bool        dialog_done     = false;  // set to true when window is destroyed

    ICoreWebView2*            webview    = nullptr;
    ICoreWebView2Controller*  controller = nullptr;
    EventRegistrationToken    resource_requested_token = {};

    HWND hwnd = nullptr;
};

// ---------------------------------------------------------------------------
// IsWebView2RuntimeAvailable
// ---------------------------------------------------------------------------

bool IsWebView2RuntimeAvailable() {
    // Check the registry for the WebView2 Runtime Evergreen installer key.
    // {F3017226-FE2A-4295-8BDF-00C3A9A7E4C5} is the Evergreen standalone GUID.
    const wchar_t* subkey = L"SOFTWARE\\Microsoft\\EdgeUpdate\\Clients\\"
                             L"{F3017226-FE2A-4295-8BDF-00C3A9A7E4C5}";

    HKEY hKey = nullptr;

    // Check HKLM first (machine-wide install)
    if (RegOpenKeyExW(HKEY_LOCAL_MACHINE, subkey, 0, KEY_READ, &hKey)
            == ERROR_SUCCESS) {
        RegCloseKey(hKey);
        return true;
    }

    // Check WOW6432Node for 32-bit process on 64-bit OS
    const wchar_t* wowSubkey =
        L"SOFTWARE\\WOW6432Node\\Microsoft\\EdgeUpdate\\Clients\\"
        L"{F3017226-FE2A-4295-8BDF-00C3A9A7E4C5}";
    if (RegOpenKeyExW(HKEY_LOCAL_MACHINE, wowSubkey, 0, KEY_READ, &hKey)
            == ERROR_SUCCESS) {
        RegCloseKey(hKey);
        return true;
    }

    // Check HKCU (per-user install)
    if (RegOpenKeyExW(HKEY_CURRENT_USER, subkey, 0, KEY_READ, &hKey)
            == ERROR_SUCCESS) {
        RegCloseKey(hKey);
        return true;
    }

    return false;
}

// ---------------------------------------------------------------------------
// WndProc
// ---------------------------------------------------------------------------

static LRESULT CALLBACK LoginWndProc(HWND hwnd, UINT msg,
                                      WPARAM wparam, LPARAM lparam) {
    WebViewLoginState* state = nullptr;

    if (msg == WM_NCCREATE) {
        auto cs = reinterpret_cast<CREATESTRUCT*>(lparam);
        state = reinterpret_cast<WebViewLoginState*>(cs->lpCreateParams);
        SetWindowLongPtrW(hwnd, GWLP_USERDATA,
                          reinterpret_cast<LONG_PTR>(state));
    } else {
        state = reinterpret_cast<WebViewLoginState*>(
            GetWindowLongPtrW(hwnd, GWLP_USERDATA));
    }

    switch (msg) {
    case WM_SIZE:
        if (state && state->controller) {
            // Position WebView2 below the banner
            RECT bounds;
            GetClientRect(hwnd, &bounds);
            bounds.top += BANNER_HEIGHT;
            state->controller->put_Bounds(bounds);
        }
        return 0;

    case WM_TOKEN_CAPTURED:
        // Token was captured by the WebResourceRequested handler.
        // DestroyWindow triggers WM_DESTROY which does cleanup.
        DestroyWindow(hwnd);
        return 0;

    case WM_CLOSE:
        DestroyWindow(hwnd);
        return 0;

    case WM_DESTROY:
        if (state) {
            // Remove the event handler before releasing
            if (state->webview &&
                state->resource_requested_token.value != 0) {
                state->webview->remove_WebResourceRequested(
                    state->resource_requested_token);
            }
            // Shut down the WebView2 browser process gracefully
            if (state->controller) {
                state->controller->Close();
            }
            // Release COM objects
            if (state->webview) {
                state->webview->Release();
                state->webview = nullptr;
            }
            if (state->controller) {
                state->controller->Release();
                state->controller = nullptr;
            }
            state->dialog_done = true;
        }
        return 0;
    }

    return DefWindowProcW(hwnd, msg, wparam, lparam);
}

// ---------------------------------------------------------------------------
// ShowWebView2LoginDialog — main entry point
// ---------------------------------------------------------------------------

std::string ShowWebView2LoginDialog(HINSTANCE h_instance, HWND parent) {
    // Quick check: if WebView2 Runtime is not installed, skip entirely
    if (!IsWebView2RuntimeAvailable()) {
        return "";
    }

    // Register window class
    static bool class_registered = false;
    if (!class_registered) {
        WNDCLASSEXW wc = {};
        wc.cbSize        = sizeof(wc);
        wc.lpfnWndProc   = LoginWndProc;
        wc.hInstance      = h_instance;
        wc.hCursor        = LoadCursor(nullptr, IDC_ARROW);
        wc.hbrBackground  = reinterpret_cast<HBRUSH>(COLOR_WINDOW + 1);
        wc.lpszClassName  = LOGIN_CLASS;
        RegisterClassExW(&wc);
        class_registered = true;
    }

    WebViewLoginState state;

    // Create the window — shown immediately so WebView2 can size correctly.
    HWND hwnd = CreateWindowExW(
        WS_EX_APPWINDOW,
        LOGIN_CLASS,
        L"Discord Presence — Login",   // em dash
        WS_OVERLAPPED | WS_CAPTION | WS_SYSMENU | WS_VISIBLE | WS_CLIPCHILDREN,
        CW_USEDEFAULT, CW_USEDEFAULT,
        DLG_WIDTH, DLG_HEIGHT,
        parent, nullptr, h_instance, &state
    );
    if (!hwnd) return "";

    state.hwnd = hwnd;

    // Center on screen
    RECT rc;
    GetWindowRect(hwnd, &rc);
    int w = rc.right - rc.left;
    int h = rc.bottom - rc.top;
    SetWindowPos(hwnd, nullptr,
        (GetSystemMetrics(SM_CXSCREEN) - w) / 2,
        (GetSystemMetrics(SM_CYSCREEN) - h) / 2,
        0, 0, SWP_NOZORDER | SWP_NOSIZE);
    UpdateWindow(hwnd);

    // Create trust banner label at the top of the window
    HFONT hBannerFont = CreateFontW(
        -13, 0, 0, 0, FW_SEMIBOLD, FALSE, FALSE, FALSE,
        DEFAULT_CHARSET, OUT_DEFAULT_PRECIS, CLIP_DEFAULT_PRECIS,
        DEFAULT_QUALITY, DEFAULT_PITCH | FF_DONTCARE, L"Segoe UI");

    HWND hBanner = CreateWindowExW(0, L"STATIC", L"",
        WS_CHILD | WS_VISIBLE | SS_CENTER,
        0, 0, DLG_WIDTH, BANNER_HEIGHT,
        hwnd, reinterpret_cast<HMENU>(static_cast<intptr_t>(ID_LABEL_BANNER)),
        h_instance, nullptr);
    SendMessageW(hBanner, WM_SETFONT, reinterpret_cast<WPARAM>(hBannerFont), TRUE);
    SetWindowTextW(hBanner,
        L"Log in to Discord to set your custom status.\r\n"
        L"Your credentials stay between you and Discord — this app never sees your password.");

    // Build user data folder path (persistent temp for session survival)
    wchar_t temp_path[MAX_PATH] = {};
    GetTempPathW(MAX_PATH, temp_path);
    std::wstring user_data_folder = std::wstring(temp_path) +
        L"DiscordPresenceUpdater_WebView2";

    // ---------------------------------------------------------------
    // Create WebView2 environment (async callback chain)
    // ---------------------------------------------------------------

    // Step 3 handler: intercept API requests and capture Authorization header
    auto resource_handler =
        Callback<ICoreWebView2WebResourceRequestedEventHandler>(
            [&state](ICoreWebView2* /*sender*/,
                     ICoreWebView2WebResourceRequestedEventArgs* args)
            -> HRESULT {
        // Already captured, ignore subsequent requests
        if (state.confirmed) return S_OK;

        // Get the request object
        ComPtr<ICoreWebView2WebResourceRequest> request;
        HRESULT hr = args->get_Request(&request);
        if (FAILED(hr)) return S_OK;

        // Get request headers
        ComPtr<ICoreWebView2HttpRequestHeaders> headers;
        hr = request->get_Headers(&headers);
        if (FAILED(hr)) return S_OK;

        // Check for Authorization header
        BOOL has_auth = FALSE;
        headers->Contains(L"Authorization", &has_auth);
        if (!has_auth) return S_OK;

        // Read the Authorization value
        wchar_t* auth_value_raw = nullptr;
        headers->GetHeader(L"Authorization", &auth_value_raw);
        if (!auth_value_raw) return S_OK;

        std::wstring wide(auth_value_raw);
        CoTaskMemFree(auth_value_raw);

        // Convert wide string to UTF-8 narrow string
        int len = WideCharToMultiByte(CP_UTF8, 0, wide.c_str(), -1,
                                       nullptr, 0, nullptr, nullptr);
        if (len <= 0) return S_OK;
        std::string narrow(len - 1, '\0');
        WideCharToMultiByte(CP_UTF8, 0, wide.c_str(), -1,
                            narrow.data(), len, nullptr, nullptr);

        // Skip bot tokens (prefix "Bot ")
        if (narrow.compare(0, 4, "Bot ") == 0) return S_OK;

        // Skip empty tokens
        if (narrow.empty()) return S_OK;

        // Token captured!
        state.token = narrow;
        state.confirmed = true;
        PostMessage(state.hwnd, WM_TOKEN_CAPTURED, 0, 0);

        return S_OK;
    });

    // Step 2 handler: controller created — set up filters, register handler, navigate
    auto ctrl_handler =
        Callback<ICoreWebView2CreateCoreWebView2ControllerCompletedHandler>(
            [&state, &resource_handler](HRESULT hr,
                                         ICoreWebView2Controller* ctrl)
            -> HRESULT {
        if (FAILED(hr) || !ctrl) {
            state.creation_failed = true;
            PostMessage(state.hwnd, WM_CLOSE, 0, 0);
            return S_OK;
        }

        state.controller = ctrl;
        ctrl->AddRef();

        // Get the CoreWebView2 instance
        HRESULT res = ctrl->get_CoreWebView2(&state.webview);
        if (FAILED(res) || !state.webview) {
            state.creation_failed = true;
            PostMessage(state.hwnd, WM_CLOSE, 0, 0);
            return S_OK;
        }
        state.webview->AddRef();

        // Add resource request filters to intercept Discord API calls.
        // Use FETCH in addition to XML_HTTP_REQUEST since modern Discord
        // uses the Fetch API for network requests.
        state.webview->AddWebResourceRequestedFilter(
            L"https://discord.com/api/*",
            COREWEBVIEW2_WEB_RESOURCE_CONTEXT_XML_HTTP_REQUEST);
        state.webview->AddWebResourceRequestedFilter(
            L"https://discordapp.com/api/*",
            COREWEBVIEW2_WEB_RESOURCE_CONTEXT_XML_HTTP_REQUEST);
        state.webview->AddWebResourceRequestedFilter(
            L"https://discord.com/api/*",
            COREWEBVIEW2_WEB_RESOURCE_CONTEXT_FETCH);
        state.webview->AddWebResourceRequestedFilter(
            L"https://discordapp.com/api/*",
            COREWEBVIEW2_WEB_RESOURCE_CONTEXT_FETCH);

        // Register the handler that captures Authorization headers
        state.webview->add_WebResourceRequested(
            resource_handler.Get(),
            &state.resource_requested_token);

        // Resize WebView2 to fill below the banner
        RECT bounds;
        GetClientRect(state.hwnd, &bounds);
        bounds.top += BANNER_HEIGHT;
        ctrl->put_Bounds(bounds);

        // Navigate to Discord login
        state.webview->Navigate(L"https://discord.com/login");

        state.webview_ready = true;
        return S_OK;
    });

    // Step 1 handler: environment created — create controller
    auto env_handler =
        Callback<ICoreWebView2CreateCoreWebView2EnvironmentCompletedHandler>(
            [&state, &ctrl_handler](HRESULT hr,
                                      ICoreWebView2Environment* env)
            -> HRESULT {
        if (FAILED(hr) || !env) {
            state.creation_failed = true;
            PostMessage(state.hwnd, WM_CLOSE, 0, 0);
            return S_OK;
        }

        env->CreateCoreWebView2Controller(state.hwnd, ctrl_handler.Get());
        return S_OK;
    });

    // Kick off the async WebView2 creation
    HRESULT create_hr = CreateCoreWebView2EnvironmentWithOptions(
        nullptr,                    // browserExecutableFolder (use installed)
        user_data_folder.c_str(),  // userDataFolder
        nullptr,                    // environmentOptions
        env_handler.Get()
    );

    if (FAILED(create_hr)) {
        // WebView2 creation failed — clean up and fall through
        DeleteObject(hBannerFont);
        DestroyWindow(hwnd);
        UnregisterClassW(LOGIN_CLASS, h_instance);
        return "";
    }

    // ---------------------------------------------------------------
    // Run modal message loop (blocks until window is destroyed)
    // Do NOT use PostQuitMessage — it would leak WM_QUIT into the
    // calling app's message loop and exit the entire application.
    // ---------------------------------------------------------------

    MSG msg;
    while (GetMessageW(&msg, nullptr, 0, 0)) {
        TranslateMessage(&msg);
        DispatchMessageW(&msg);
        if (state.dialog_done) break;
    }

    // Cleanup
    DeleteObject(hBannerFont);
    UnregisterClassW(LOGIN_CLASS, h_instance);

    return state.confirmed ? state.token : "";
}