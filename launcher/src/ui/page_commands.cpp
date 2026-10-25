#include "ui/page_commands.h"

#include <cstdlib>

#include "system/files.h"
#include "system/json.h"
#include "system/log.h"
#include "system/process.h"
#include "system/text.h"
#include "ui/folder_picker.h"
#include "ui/view_state.h"

namespace oreo {

namespace {

struct Message {
    std::string action;
    std::string id;
    std::string json;   // the whole message, for commands that carry more than an id
    size_t index() const { return (size_t)atoi(id.c_str()); }
};

using Handler = void (*)(WindowState&, const Message&);

// The window runs its own move or resize from here, exactly as if its frame had been grabbed.
// Posted, not sent: the move loop must not start inside the web view's message callback.
void start_window_move(HWND hwnd, WPARAM hit) {
    POINT cursor{};
    GetCursorPos(&cursor);
    ReleaseCapture();
    PostMessageW(hwnd, WM_NCLBUTTONDOWN, hit, MAKELPARAM(cursor.x, cursor.y));
}

// A press on the page's background. Not WebView2's own `app-region: drag`: in a windowed web
// view that is a hidden child window per drag rectangle, all moved again whenever the page
// reports its regions, and fast scrolling queued seconds of that ahead of every click. Only
// while the button is still down: after a quick click the move would wait for a release that
// already went to the page.
void drag(WindowState& w, const Message&) {
    const int button = GetSystemMetrics(SM_SWAPBUTTON) ? VK_RBUTTON : VK_LBUTTON;
    if (GetAsyncKeyState(button) & 0x8000) start_window_move(w.hwnd, HTCAPTION);
}

// One of the page's edge strips.
void resize(WindowState& w, const Message& m) {
    static const struct {
        const char* edge;
        WPARAM hit;
    } kEdges[] = {{"t", HTTOP},       {"b", HTBOTTOM},     {"l", HTLEFT},          {"r", HTRIGHT},
                  {"tl", HTTOPLEFT}, {"tr", HTTOPRIGHT}, {"bl", HTBOTTOMLEFT}, {"br", HTBOTTOMRIGHT}};
    if (IsZoomed(w.hwnd)) return;
    for (const auto& e : kEdges)
        if (m.id == e.edge) start_window_move(w.hwnd, e.hit);
}

void toggle_mod(WindowState& w, const Message& m) {
    std::string error;
    if (!w.app->toggle_mod(m.index(), error)) {
        Log::warn("mod switch refused: " + error);
        w.repaint_all();   // the switch on the page already moved: put it back
    }
}

void refresh(WindowState& w, const Message&) {
    w.cards.resort();   // once the check is over
    w.app->recheck_async();
}

void show_settings(WindowState& w, const Message&) {
    w.call_page("showSettings(" +
                settings_json(w.app->settings_values(), w.app->detected_game_path(), w.app->detected_mods_path()) +
                ");");
}

void browse(WindowState& w, const Message& m) {
    const bool game = m.action == "browsegame";
    AppSnapshot snap = w.app->snapshot();
    std::string picked = pick_folder(w.hwnd, game ? L"Select the No Man's Sky folder" : L"Select the mods folder",
                                     game ? snap.game_path : snap.mods_path);
    w.call_page("setSettingsPath(" + json_quote(game ? "game" : "mods") + ", " + json_quote(picked) + ");");
}

void save_settings(WindowState& w, const Message& m) {
    SettingsValues values;
    values.game_path = json_field(m.json, "gamePath");
    values.mods_path = json_field(m.json, "modsPath");
    values.always_show_window = json_field(m.json, "alwaysShow") == "1";
    values.check_for_updates = json_field(m.json, "checkUpdates") == "1";
    values.high_contrast = json_field(m.json, "highContrast") == "1";
    values.debug_hotkeys = json_field(m.json, "debugHotkeys") == "1";
    values.verbose = json_field(m.json, "verbose") == "1";

    SettingsErrors errors;
    std::string error;
    if (w.app->apply_settings(values, errors, error)) {
        w.repaint_all();   // the mod list, the paths and the theme may all have changed
        w.call_page("closeSettings();");
    } else {
        w.call_page("settingsFailed(" + json_quote(errors.game_path) + ", " + json_quote(errors.mods_path) + ", " +
                    json_quote(error) + ");");
    }
}

// The test version is this executable in a second process, so it can never touch this one.
void open_test_version(WindowState&, const Message&) {
    std::string error;
    if (!run_detached(exe_path(), {"--test-ui"}, EnvMap(), path_dir(exe_path()), nullptr, &error, true))
        Log::warn("the test version could not be started: " + error);
}

void open_mod_folder(WindowState& w, const Message& m) {
    AppSnapshot snap = w.app->snapshot();
    const size_t index = m.index();
    if (index < snap.mods.size() && !snap.mods[index].path.empty())   // test mods have no folder
        shell_open(snap.mods[index].path);
}

// Only a plain https GitHub address. The page is ours, but "the page is ours" is not a check.
void open_url(WindowState&, const Message& m) {
    const std::string prefix = "https://github.com/";
    const bool safe = starts_with(m.id, prefix) && m.id.find_first_of(" \"'<>\r\n\t") == std::string::npos;
    if (safe) shell_open(m.id);
    else Log::warn("refused to open a link that is not a plain GitHub address: " + m.id);
}

void show_details(WindowState& w, const Message& m) {
    for (const Component& c : w.app->snapshot().components) {
        if (c.id != m.id) continue;
        const OperationDetails& d = c.details;
        std::string body = "<div style='color:#d2d8de'>" + c.error + "</div><pre>" + "command: " + d.command +
                           "\nexit code: " + d.exit_code + "\npython: " + d.python_path +
                           "\nversions: " + d.package_version + "\ntime: " + d.timestamp + "\n\n" + d.out + "\n" +
                           d.err + "</pre>";
        w.call_page("showDialog(" + json_quote(c.name) + ", " + json_quote(body) + ", false);");
        break;
    }
}

const struct {
    const char* action;
    Handler handler;
} kCommands[] = {
    {"play", [](WindowState& w, const Message&) { w.played = true; w.app->play_async(); }},
    // Counts as played: closing the window afterwards must not cancel the game it started.
    {"playvanilla", [](WindowState& w, const Message&) { w.played = true; w.app->play_without_mods_async(); }},
    {"minimize", [](WindowState& w, const Message&) { ShowWindow(w.hwnd, SW_MINIMIZE); }},
    {"maximize", [](WindowState& w, const Message&) { ShowWindow(w.hwnd, IsZoomed(w.hwnd) ? SW_RESTORE : SW_MAXIMIZE); }},
    {"close", [](WindowState& w, const Message&) { PostMessageW(w.hwnd, WM_CLOSE, 0, 0); }},
    {"drag", drag},
    {"resize", resize},
    {"theme", [](WindowState& w, const Message& m) { w.app->set_light_theme(m.id == "light"); }},
    {"togglemod", toggle_mod},
    {"prepare", [](WindowState& w, const Message& m) { w.app->prepare_mod_async(m.index()); }},
    {"action", [](WindowState& w, const Message& m) { w.app->run_action(m.id); }},
    {"refresh", refresh},
    {"settings", show_settings},
    // A developer escape hatch: everything in that file is in the Settings window too.
    {"openconfig", [](WindowState& w, const Message&) { shell_open(w.app->config().config_path); }},
    {"browsegame", browse},
    {"browsemods", browse},
    {"savesettings", save_settings},
    {"updatelauncher", [](WindowState& w, const Message&) { w.app->update_launcher_async(); }},
    {"testui", open_test_version},
    {"openmodfolder", open_mod_folder},
    {"openurl", open_url},
    {"details", show_details},
};

}  // namespace

void handle_page_message(WindowState& window, const std::string& json) {
    Message message{json_field(json, "action"), json_field(json, "id"), json};
    Log::debug("ui action: " + message.action + " " + message.id);
    bool known = false;
    for (const auto& command : kCommands) {
        if (message.action != command.action) continue;
        command.handler(window, message);
        known = true;
        break;
    }
    if (!known) Log::warn("the page sent an unknown action: " + message.action);
    window.push_state();
}

}  // namespace oreo
