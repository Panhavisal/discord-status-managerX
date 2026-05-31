#include "setup_dialog.h"
#include "token_extractor.h"

#include <shellapi.h>
#include <string>
#include <memory>

// ---------------------------------------------------------------------------
// Layout constants
// ---------------------------------------------------------------------------

static constexpr int DLG_WIDTH   = 440;
static constexpr int MARGIN      = 16;
static constexpr int LABEL_H     = 16;
static constexpr int EDIT_H      = 26;
static constexpr int BTN_H       = 28;
static constexpr int LINK_H      = 26;
static constexpr int GAP         = 10;

// Control IDs
static constexpr int ID_EDIT_TOKEN    = 1001;
static constexpr int ID_LINK_HELP     = 1002;

// ---------------------------------------------------------------------------
// State passed between ShowSetupDialog and the WndProc
// ---------------------------------------------------------------------------

struct SetupState {
    std::string token;
    bool        confirmed = false;
    bool        dialog_done = false;
};

// ---------------------------------------------------------------------------
// WndProc
// ---------------------------------------------------------------------------

static const wchar_t SETUP_CLASS[] = L"DiscordPresenceSetupDlg";

static LRESULT CALLBACK SetupWndProc(HWND hwnd, UINT msg,
                                      WPARAM wparam, LPARAM lparam) {
    SetupState* state = nullptr;

    if (msg == WM_NCCREATE) {
        auto cs = reinterpret_cast<CREATESTRUCT*>(lparam);
        state = reinterpret_cast<SetupState*>(cs->lpCreateParams);
        SetWindowLongPtrW(hwnd, GWLP_USERDATA, reinterpret_cast<LONG_PTR>(state));
    } else {
        state = reinterpret_cast<SetupState*>(GetWindowLongPtrW(hwnd, GWLP_USERDATA));
    }

    switch (msg) {
    case WM_COMMAND: {
        switch (LOWORD(wparam)) {
        case ID_EDIT_TOKEN:
            if (HIWORD(wparam) == EN_CHANGE) {
                HWND hOk = GetDlgItem(hwnd, IDOK);
                int len = GetWindowTextLengthA(GetDlgItem(hwnd, ID_EDIT_TOKEN));
                EnableWindow(hOk, len > 0);
            }
            return 0;

        case IDOK: {
            char buf[256] = {};
            GetDlgItemTextA(hwnd, ID_EDIT_TOKEN, buf, 256);
            std::string entered(buf);

            // Trim whitespace
            while (!entered.empty() && (entered.front() == ' ' || entered.front() == '\t'))
                entered.erase(entered.begin());
            while (!entered.empty() && (entered.back() == ' ' || entered.back() == '\t'))
                entered.pop_back();

            if (entered.empty()) {
                MessageBoxW(hwnd,
                    L"Please enter your Discord token.",
                    L"Missing token", MB_OK | MB_ICONWARNING);
                return 0;
            }

            // Validate the token
            std::string username;
            if (!TokenExtractor::ValidateToken(entered, username)) {
                int result = MessageBoxW(hwnd,
                    L"This token doesn't appear to be valid.\n"
                    L"Use it anyway?",
                    L"Invalid token", MB_YESNO | MB_ICONWARNING);
                if (result != IDYES) return 0;
            }

            state->token = entered;
            state->confirmed = true;
            DestroyWindow(hwnd);
            return 0;
        }

        case IDCANCEL:
            DestroyWindow(hwnd);
            return 0;

        case ID_LINK_HELP:
            ShellExecuteW(nullptr, L"open",
                L"https://support.discord.com/hc/en-us/articles/5726112400411",
                nullptr, nullptr, SW_SHOWNORMAL);
            return 0;
        }
        break;
    }

    case WM_CLOSE:
        DestroyWindow(hwnd);
        return 0;

    case WM_DESTROY:
        state->dialog_done = true;
        return 0;
    }

    return DefWindowProcW(hwnd, msg, wparam, lparam);
}

// ---------------------------------------------------------------------------
// ShowSetupDialog
// ---------------------------------------------------------------------------

std::string ShowSetupDialog(HINSTANCE h_instance, HWND parent) {
    static bool class_registered = false;
    if (!class_registered) {
        WNDCLASSEXW wc = {};
        wc.cbSize        = sizeof(wc);
        wc.lpfnWndProc   = SetupWndProc;
        wc.hInstance      = h_instance;
        wc.hCursor        = LoadCursor(nullptr, IDC_ARROW);
        wc.hbrBackground  = reinterpret_cast<HBRUSH>(COLOR_WINDOW + 1);
        wc.lpszClassName  = SETUP_CLASS;
        RegisterClassExW(&wc);
        class_registered = true;
    }

    SetupState state;

    // Calculate total content height
    int contentW = DLG_WIDTH - 2 * MARGIN;
    int y = MARGIN;

    y += 24 + GAP;          // Title
    y += LABEL_H * 4 + 12 + GAP; // 4 instruction lines
    y += LABEL_H + GAP + EDIT_H + GAP;  // Edit label + edit
    y += LINK_H + GAP * 2;
    y += BTN_H;
    y += MARGIN;
    int winH = y + 30;

    HWND hwnd = CreateWindowExW(
        WS_EX_DLGMODALFRAME | WS_EX_WINDOWEDGE,
        SETUP_CLASS,
        L"Discord Presence — Token Setup",
        WS_OVERLAPPED | WS_CAPTION | WS_SYSMENU,
        CW_USEDEFAULT, CW_USEDEFAULT,
        DLG_WIDTH, winH,
        parent, nullptr, h_instance, &state
    );
    if (!hwnd) return "";

    // Center on screen
    RECT rc;
    GetWindowRect(hwnd, &rc);
    int w = rc.right - rc.left;
    int h = rc.bottom - rc.top;
    SetWindowPos(hwnd, nullptr,
        (GetSystemMetrics(SM_CXSCREEN) - w) / 2,
        (GetSystemMetrics(SM_CYSCREEN) - h) / 2,
        0, 0, SWP_NOZORDER | SWP_NOSIZE);

    HFONT hFont = reinterpret_cast<HFONT>(GetStockObject(DEFAULT_GUI_FONT));
    HFONT hFontBold = CreateFontW(
        -15, 0, 0, 0, FW_BOLD, FALSE, FALSE, FALSE,
        DEFAULT_CHARSET, OUT_DEFAULT_PRECIS, CLIP_DEFAULT_PRECIS,
        DEFAULT_QUALITY, DEFAULT_PITCH | FF_DONTCARE, L"Segoe UI");

    y = MARGIN;

    // Title
    HWND hTitle = CreateWindowExW(0, L"STATIC",
        L"Discord Token Required",
        WS_CHILD | WS_VISIBLE,
        MARGIN, y, contentW, 24, hwnd, nullptr, h_instance, nullptr);
    SendMessageW(hTitle, WM_SETFONT, reinterpret_cast<WPARAM>(hFontBold), TRUE);
    y += 24 + GAP;

    // Instructions
    HWND hInstr = CreateWindowExW(0, L"STATIC", L"",
        WS_CHILD | WS_VISIBLE | SS_LEFT,
        MARGIN, y, contentW, LABEL_H * 4 + 12,
        hwnd, nullptr, h_instance, nullptr);
    SendMessageW(hInstr, WM_SETFONT, reinterpret_cast<WPARAM>(hFont), TRUE);
    SetWindowTextW(hInstr,
        L"Could not acquire your Discord token automatically.\n"
        L"To get your token manually:\n"
        L"1. Open Discord in a browser and log in\n"
        L"2. Press F12 → Network tab → make any action\n"
        L"3. Find an API request → copy the Authorization header value");
    y += LABEL_H * 4 + 12 + GAP;

    // Edit label
    HWND hEditLabel = CreateWindowExW(0, L"STATIC",
        L"Discord Token:",
        WS_CHILD | WS_VISIBLE | SS_LEFT,
        MARGIN, y, contentW, LABEL_H, hwnd, nullptr, h_instance, nullptr);
    SendMessageW(hEditLabel, WM_SETFONT, reinterpret_cast<WPARAM>(hFont), TRUE);
    y += LABEL_H + GAP;

    // Edit control
    HWND hEdit = CreateWindowExW(WS_EX_CLIENTEDGE, L"EDIT", L"",
        WS_CHILD | WS_VISIBLE | WS_TABSTOP | ES_AUTOHSCROLL,
        MARGIN, y, contentW, EDIT_H, hwnd,
        reinterpret_cast<HMENU>(static_cast<intptr_t>(ID_EDIT_TOKEN)),
        h_instance, nullptr);
    SendMessageW(hEdit, WM_SETFONT, reinterpret_cast<WPARAM>(hFont), TRUE);
    SendMessageW(hEdit, EM_SETLIMITTEXT, 128, 0);
    y += EDIT_H + GAP;

    // Help link
    HWND hLink = CreateWindowExW(0, L"BUTTON",
        L"Open Discord Help  →",
        WS_CHILD | WS_VISIBLE | BS_PUSHBUTTON,
        MARGIN, y, contentW, LINK_H, hwnd,
        reinterpret_cast<HMENU>(static_cast<intptr_t>(ID_LINK_HELP)),
        h_instance, nullptr);
    SendMessageW(hLink, WM_SETFONT, reinterpret_cast<WPARAM>(hFont), TRUE);
    y += LINK_H + GAP * 2;

    // OK / Cancel buttons
    int btnW = 90;
    int btnGap = 12;
    int btnX = MARGIN + contentW - btnW * 2 - btnGap;

    HWND hOk = CreateWindowExW(0, L"BUTTON", L"OK",
        WS_CHILD | WS_VISIBLE | WS_TABSTOP | BS_DEFPUSHBUTTON,
        btnX, y, btnW, BTN_H, hwnd,
        reinterpret_cast<HMENU>(IDOK), h_instance, nullptr);
    SendMessageW(hOk, WM_SETFONT, reinterpret_cast<WPARAM>(hFont), TRUE);
    EnableWindow(hOk, FALSE);

    HWND hCancel = CreateWindowExW(0, L"BUTTON", L"Cancel",
        WS_CHILD | WS_VISIBLE | WS_TABSTOP | BS_PUSHBUTTON,
        btnX + btnW + btnGap, y, btnW, BTN_H, hwnd,
        reinterpret_cast<HMENU>(IDCANCEL), h_instance, nullptr);
    SendMessageW(hCancel, WM_SETFONT, reinterpret_cast<WPARAM>(hFont), TRUE);

    // Show and run modal loop
    ShowWindow(hwnd, SW_SHOW);
    UpdateWindow(hwnd);
    SetFocus(hEdit);

    MSG msg;
    while (GetMessageW(&msg, nullptr, 0, 0)) {
        if (!IsDialogMessageW(hwnd, &msg)) {
            TranslateMessage(&msg);
            DispatchMessageW(&msg);
        }
        if (state.dialog_done) break;
    }

    DeleteObject(hFontBold);
    UnregisterClassW(SETUP_CLASS, h_instance);
    class_registered = false;

    return state.confirmed ? state.token : "";
}

// ---------------------------------------------------------------------------
// Auto-extract token (with progress dialog)
// ---------------------------------------------------------------------------

static const wchar_t PROGRESS_CLASS[] = L"DiscordPresenceProgressDlg";

static LRESULT CALLBACK ProgressWndProc(HWND hwnd, UINT msg,
                                          WPARAM wparam, LPARAM lparam) {
    switch (msg) {
    case WM_CLOSE:
        // Don't allow closing the progress dialog
        return 0;
    }
    return DefWindowProcW(hwnd, msg, wparam, lparam);
}

std::string TryAutoExtractToken(HINSTANCE h_instance) {
    // Show a small "extracting" window
    static bool class_registered = false;
    if (!class_registered) {
        WNDCLASSEXW wc = {};
        wc.cbSize        = sizeof(wc);
        wc.lpfnWndProc   = ProgressWndProc;
        wc.hInstance      = h_instance;
        wc.hCursor        = LoadCursor(nullptr, IDC_ARROW);
        wc.hbrBackground  = reinterpret_cast<HBRUSH>(COLOR_WINDOW + 1);
        wc.lpszClassName  = PROGRESS_CLASS;
        RegisterClassExW(&wc);
        class_registered = true;
    }

    HWND hwnd = CreateWindowExW(
        WS_EX_DLGMODALFRAME | WS_EX_WINDOWEDGE,
        PROGRESS_CLASS,
        L"Discord Presence — Extracting Token",
        WS_OVERLAPPED | WS_CAPTION,
        CW_USEDEFAULT, CW_USEDEFAULT, 320, 80,
        nullptr, nullptr, h_instance, nullptr
    );

    HFONT hFont = reinterpret_cast<HFONT>(GetStockObject(DEFAULT_GUI_FONT));
    HWND hLabel = CreateWindowExW(0, L"STATIC",
        L"Scanning Discord local storage for token...",
        WS_CHILD | WS_VISIBLE | SS_CENTER,
        10, 15, 280, 20, hwnd, nullptr, h_instance, nullptr);
    SendMessageW(hLabel, WM_SETFONT, reinterpret_cast<WPARAM>(hFont), TRUE);

    // Center
    RECT rc;
    GetWindowRect(hwnd, &rc);
    SetWindowPos(hwnd, nullptr,
        (GetSystemMetrics(SM_CXSCREEN) - (rc.right - rc.left)) / 2,
        (GetSystemMetrics(SM_CYSCREEN) - (rc.bottom - rc.top)) / 2,
        0, 0, SWP_NOZORDER | SWP_NOSIZE);

    ShowWindow(hwnd, SW_SHOW);
    UpdateWindow(hwnd);

    // Do the extraction
    auto tokens = TokenExtractor::Extract();

    // Find the first valid token
    std::string valid_token;
    for (auto& t : tokens) {
        if (t.is_valid && !t.token.empty()) {
            valid_token = t.token;
            break;
        }
    }

    // If no pre-validated token, try validating remaining ones
    if (valid_token.empty()) {
        for (auto& t : tokens) {
            if (!t.token.empty()) {
                std::string username;
                if (TokenExtractor::ValidateToken(t.token, username)) {
                    valid_token = t.token;
                    break;
                }
            }
        }
    }

    DestroyWindow(hwnd);
    UnregisterClassW(PROGRESS_CLASS, h_instance);
    class_registered = false;

    return valid_token;
}