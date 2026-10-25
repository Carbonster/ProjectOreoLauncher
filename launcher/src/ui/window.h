// window.h - the launcher window: a plain frameless window hosting the page in WebView2.
//
// The page draws everything, including its own minimise, maximise and close buttons, and never
// decides anything: the window sends it the state as JSON and runs what it posts back.
#pragma once

#include "app/app.h"

namespace oreo {

// Runs until the player closes the window or the game has been started. True when PLAY or
// PLAY WITHOUT MODS was pressed.
bool run_ui(App& app);

}  // namespace oreo
