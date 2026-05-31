#pragma once

#include <string>
#include <windows.h>

// Shows a modal setup dialog when token extraction fails.
// Allows manual token entry as fallback.
// Returns the entered token, or empty string if the user cancelled.
std::string ShowSetupDialog(HINSTANCE h_instance, HWND parent);

// Shows a brief "extracting token..." message and attempts auto-extraction.
// Returns the valid token, or empty string if extraction failed.
std::string TryAutoExtractToken(HINSTANCE h_instance);