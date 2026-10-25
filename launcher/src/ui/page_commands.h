// page_commands.h - what the window does when the page posts {action, id}.
#pragma once

#include <string>

#include "ui/window_state.h"

namespace oreo {

// Runs the command in `json`, then sends the page the new state. Unknown actions are logged.
void handle_page_message(WindowState& window, const std::string& json);

}  // namespace oreo
