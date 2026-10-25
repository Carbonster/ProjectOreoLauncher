// folder_picker.h - the standard Windows folder picker.
#pragma once

#include <windows.h>

#include <string>

namespace oreo {

// A real filesystem path, or "" when the player cancelled.
std::string pick_folder(HWND owner, const wchar_t* title, const std::string& initial);

}  // namespace oreo
