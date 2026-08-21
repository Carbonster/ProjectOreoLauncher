// ui.h - the launcher window.
//
// The UI renders the snapshot the App hands it and reports button presses back. It contains no
// version logic, no pip calls and no path detection - that all lives in the services.
#pragma once

#include "../core/app.h"

namespace oreo {

// Opens the window and runs until the user closes it or the game has been launched.
// Returns true when PLAY was pressed (the caller then exits quietly).
bool run_ui(App& app);

}  // namespace oreo
