#pragma once

#include <string>
#include <windows.h>

// Opens a WebView2 window pointing to Discord's login page.
// Captures the user token from outgoing Authorization headers on API requests.
// Returns the token if successful, or empty string if cancelled/failed.
// If the WebView2 Runtime is not installed, returns empty immediately.
std::string ShowWebView2LoginDialog(HINSTANCE h_instance, HWND parent);

// Returns true if the WebView2 Runtime appears to be installed.
// Checks the Windows registry for the EdgeUpdate Evergreen installer key.
bool IsWebView2RuntimeAvailable();