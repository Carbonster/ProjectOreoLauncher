#include "ui/window.h"

#include <windows.h>
#include <wrl/event.h>

#include <algorithm>
#include <atomic>

#include "system/files.h"
#include "system/log.h"
#include "system/text.h"
#include "ui/page.h"
#include "ui/page_commands.h"
#include "ui/view_state.h"
#include "ui/window_state.h"

#pragma comment(lib, "WebView2LoaderStatic.lib")
#pragma comment(lib, "ole32.lib")
#pragma comment(lib, "shell32.lib")

using Microsoft::WRL::Callback;
using Microsoft::WRL::ComPtr;

namespace oreo {

namespace {

// The stage the page is laid out on, in CSS pixels. The window keeps these proportions.
const int kStageW = 1568;
const int kStageH = 906;
const UINT_PTR kRefreshTimer = 1;
const UINT kRefreshMs = 300;

// The one window there is. wnd_proc is a plain callback, so it is reached through here.
WindowState g_window;

// The page's ground (--m-ground in high contrast), painted until the page has drawn.
COLORREF theme_background(bool light, bool high_contrast) {
    if (high_contrast) return light ? RGB(0xb3, 0x14, 0x0c) : RGB(0x6e, 0x92, 0xbf);
    return light ? RGB(0xec, 0xe6, 0xd8) : RGB(0x0b, 0x0b, 0x0b);
}

void set_webview_background(COLORREF colour) {
    ComPtr<ICoreWebView2Controller2> controller2;
    if (g_window.controller && SUCCEEDED(g_window.controller.As(&controller2)))
        controller2->put_DefaultBackgroundColor({255, GetRValue(colour), GetGValue(colour), GetBValue(colour)});
}

// Follows the theme, or a dark window opens on a white flash.
void apply_theme_colour(bool light, bool high_contrast) {
    COLORREF colour = theme_background(light, high_contrast);
    if (colour == g_window.frame_color) return;
    g_window.frame_color = colour;
    if (g_window.hwnd) InvalidateRect(g_window.hwnd, nullptr, TRUE);
    set_webview_background(colour);
}

// Whichever edge is dragged, the other side follows, so the page always fills the window.
void keep_proportions(WPARAM edge, RECT* r) {
    int width = std::max<int>(r->right - r->left, kStageW / 2);
    int height = std::max<int>(r->bottom - r->top, kStageH / 2);
    if (edge == WMSZ_TOP || edge == WMSZ_BOTTOM) {
        width = MulDiv(height, kStageW, kStageH);
        r->right = r->left + width;
        if (edge == WMSZ_TOP) r->top = r->bottom - height;
        else r->bottom = r->top + height;
        return;
    }
    height = MulDiv(width, kStageH, kStageW);
    if (edge == WMSZ_LEFT || edge == WMSZ_TOPLEFT || edge == WMSZ_BOTTOMLEFT) r->left = r->right - width;
    else r->right = r->left + width;
    if (edge == WMSZ_TOPLEFT || edge == WMSZ_TOPRIGHT) r->top = r->bottom - height;
    else r->bottom = r->top + height;
}

void close_window(HWND hwnd) {
    g_window.closing = true;
    PostMessageW(hwnd, WM_CLOSE, 0, 0);
}

// Every refresh: a new state for the page, and whether the window's work is done.
void on_refresh_timer(HWND hwnd) {
    g_window.push_state();
    App* app = g_window.app;
    if (!app) return;
    // The launcher replaced itself: main() starts the new one. Never while an operation runs -
    // the update still holds the gate for an instant after it names the new exe, so the close
    // simply happens on the next tick.
    if (!app->pending_restart().empty() && app->snapshot().busy_with.empty()) return close_window(hwnd);
    // The game has been started with its mods: nothing left to show.
    if (app->play_finished()) close_window(hwnd);
}

LRESULT CALLBACK wnd_proc(HWND hwnd, UINT msg, WPARAM wparam, LPARAM lparam) {
    switch (msg) {
        case WM_NCCALCSIZE:
            // No frame: all of it is client area. A maximised window is placed with its missing
            // frame hanging off the monitor, so it is pulled back to the work area.
            if (wparam && IsZoomed(hwnd)) {
                MONITORINFO monitor{};
                monitor.cbSize = sizeof(monitor);
                if (GetMonitorInfoW(MonitorFromWindow(hwnd, MONITOR_DEFAULTTONEAREST), &monitor))
                    ((NCCALCSIZE_PARAMS*)lparam)->rgrc[0] = monitor.rcWork;
            }
            return 0;
        case WM_NCACTIVATE:
            return DefWindowProcW(hwnd, msg, wparam, -1);   // -1: no frame to repaint
        case WM_SIZING:
            keep_proportions(wparam, (RECT*)lparam);
            return TRUE;
        case WM_ERASEBKGND: {
            RECT rc;
            GetClientRect(hwnd, &rc);
            HBRUSH brush = CreateSolidBrush(g_window.frame_color);
            FillRect((HDC)wparam, &rc, brush);
            DeleteObject(brush);
            return 1;
        }
        case WM_SIZE:
            if (g_window.controller) {
                RECT rc;
                GetClientRect(hwnd, &rc);
                g_window.controller->put_Bounds(rc);
            }
            g_window.push_state();   // maximised or not decides whether the page offers resize strips
            return 0;
        case WM_GETMINMAXINFO: {
            MINMAXINFO* info = (MINMAXINFO*)lparam;
            info->ptMinTrackSize.x = kStageW / 2;
            info->ptMinTrackSize.y = kStageH / 2;
            return 0;
        }
        case WM_TIMER:
            on_refresh_timer(hwnd);
            return 0;
        case WM_CLOSE:
            g_window.closing = true;
            DestroyWindow(hwnd);
            return 0;
        case WM_DESTROY:
            PostQuitMessage(0);
            return 0;
    }
    return DefWindowProcW(hwnd, msg, wparam, lparam);
}

// The window icon is resource 1, the one Explorer shows on the exe. LoadImage with the real
// metrics picks a sharp size out of it for the taskbar and the window menu.
HICON load_icon(HINSTANCE instance, int metric_x, int metric_y) {
    return (HICON)LoadImageW(instance, MAKEINTRESOURCEW(1), IMAGE_ICON, GetSystemMetrics(metric_x),
                             GetSystemMetrics(metric_y), 0);
}

// The stage at its own size where it fits, scaled down to most of the screen where it does
// not, centred on the monitor the player is looking at.
RECT initial_placement() {
    HMONITOR monitor = nullptr;
    if (HWND foreground = GetForegroundWindow()) monitor = MonitorFromWindow(foreground, MONITOR_DEFAULTTONEAREST);
    if (!monitor) {
        POINT cursor{};
        GetCursorPos(&cursor);
        monitor = MonitorFromPoint(cursor, MONITOR_DEFAULTTONEAREST);
    }
    MONITORINFO info{};
    info.cbSize = sizeof(info);
    GetMonitorInfoW(monitor, &info);
    const RECT work = info.rcWork;
    const double scale = std::min({1.0, 0.92 * (work.right - work.left) / kStageW,
                                   0.92 * (work.bottom - work.top) / kStageH});
    const int width = (int)(kStageW * scale);
    const int height = (int)(kStageH * scale);
    const int x = work.left + ((work.right - work.left) - width) / 2;
    const int y = work.top + ((work.bottom - work.top) - height) / 2;
    return {x, y, x + width, y + height};
}

HWND create_window(const App& app) {
    WNDCLASSEXW wc{};
    wc.cbSize = sizeof(wc);
    wc.lpfnWndProc = wnd_proc;
    wc.hInstance = GetModuleHandleW(nullptr);
    wc.hCursor = LoadCursor(nullptr, IDC_ARROW);
    wc.hbrBackground = nullptr;   // WM_ERASEBKGND paints the theme's colour
    wc.lpszClassName = L"ProjectOreoLauncherV3";
    wc.hIcon = load_icon(wc.hInstance, SM_CXICON, SM_CYICON);
    wc.hIconSm = load_icon(wc.hInstance, SM_CXSMICON, SM_CYSMICON);
    RegisterClassExW(&wc);

    const RECT place = initial_placement();
    HWND hwnd = CreateWindowExW(0, wc.lpszClassName, app.test_mode() ? L"Oreo Launcher - test version" : L"Oreo Launcher",
                                WS_OVERLAPPEDWINDOW, place.left, place.top, place.right - place.left,
                                place.bottom - place.top, nullptr, nullptr, wc.hInstance, nullptr);
    if (!hwnd) return nullptr;

    // DWMWA_USE_IMMERSIVE_DARK_MODE (20): the window menu (Alt+Space) follows it.
    BOOL dark = TRUE;
    typedef HRESULT(WINAPI * SetAttr)(HWND, DWORD, LPCVOID, DWORD);
    if (HMODULE dwm = LoadLibraryW(L"dwmapi.dll")) {
        if (auto set_attr = (SetAttr)GetProcAddress(dwm, "DwmSetWindowAttribute")) set_attr(hwnd, 20, &dark, sizeof(dark));
        FreeLibrary(dwm);
    }
    // Asks WM_NCCALCSIZE again, so the frame is gone before the window is first shown.
    SetWindowPos(hwnd, nullptr, 0, 0, 0, 0, SWP_FRAMECHANGED | SWP_NOMOVE | SWP_NOSIZE | SWP_NOZORDER | SWP_NOACTIVATE);
    ShowWindow(hwnd, SW_SHOWNORMAL);
    ShowWindow(hwnd, SW_SHOWNORMAL);
    UpdateWindow(hwnd);
    return hwnd;
}

// Sets the web view up once its controller exists, and loads the page.
void attach_webview(ICoreWebView2Controller* controller) {
    g_window.controller = controller;
    g_window.controller->get_CoreWebView2(&g_window.webview);

    ComPtr<ICoreWebView2Settings> settings;
    if (SUCCEEDED(g_window.webview->get_Settings(&settings))) {
        settings->put_AreDefaultContextMenusEnabled(FALSE);
        settings->put_IsStatusBarEnabled(FALSE);
        settings->put_AreDevToolsEnabled(g_window.app->config().debug_hotkeys ? TRUE : FALSE);
        settings->put_IsZoomControlEnabled(FALSE);
        // IsNonClientRegionSupportEnabled stays off: the page moves the window ("drag").
    }
    set_webview_background(g_window.frame_color);

    EventRegistrationToken token;
    g_window.webview->add_WebMessageReceived(
        Callback<ICoreWebView2WebMessageReceivedEventHandler>(
            [](ICoreWebView2*, ICoreWebView2WebMessageReceivedEventArgs* args) -> HRESULT {
                LPWSTR raw = nullptr;
                if (SUCCEEDED(args->TryGetWebMessageAsString(&raw)) && raw) {
                    handle_page_message(g_window, to_utf8(raw));
                    CoTaskMemFree(raw);
                }
                return S_OK;
            })
            .Get(),
        &token);

    RECT rc;
    GetClientRect(g_window.hwnd, &rc);
    g_window.controller->put_Bounds(rc);
    const Config& cfg = g_window.app->config();
    g_window.webview->NavigateToString(to_wide(page_html(cfg.light_theme, cfg.high_contrast)).c_str());
}

}  // namespace

void WindowState::call_page(const std::string& javascript) const {
    if (webview) webview->ExecuteScript(to_wide(javascript).c_str(), nullptr);
}

void WindowState::push_state() {
    if (!webview || !app) return;
    AppSnapshot snap = app->snapshot();
    apply_theme_colour(snap.light_theme, snap.high_contrast);
    thumbnails.sync(snap.mods, [this](const std::string& js) { call_page(js); });   // before the state names them

    ViewOptions options;
    options.maximized = hwnd && IsZoomed(hwnd);
    options.debug_hotkeys = app->config().debug_hotkeys;
    options.card_order = cards.order(snap);
    options.thumbnails = &thumbnails;
    std::string json = state_json(snap, options);
    if (json == last_state) return;   // nothing changed: leave the page alone
    last_state = json;
    call_page("render(" + json + ");");
}

bool run_ui(App& app) {
    g_window = WindowState();
    g_window.app = &app;
    g_window.frame_color = theme_background(app.config().light_theme, app.config().high_contrast);
    g_window.hwnd = create_window(app);
    if (!g_window.hwnd) {
        Log::error("could not create the launcher window");
        return false;
    }

    // WebView2 keeps its profile here, not next to the game files. The test version runs next
    // to a real launcher and keeps its own.
    std::string data_dir = path_join(app.config().oreo_dir, app.test_mode() ? "webview-test" : "webview");
    make_dirs(data_dir);

    std::atomic<bool> ready{false};
    std::atomic<bool> failed{false};
    HRESULT hr = CreateCoreWebView2EnvironmentWithOptions(
        nullptr, to_wide(data_dir).c_str(), nullptr,
        Callback<ICoreWebView2CreateCoreWebView2EnvironmentCompletedHandler>(
            [&ready, &failed](HRESULT result, ICoreWebView2Environment* env) -> HRESULT {
                if (FAILED(result) || !env) {
                    failed = true;
                    return S_OK;
                }
                env->CreateCoreWebView2Controller(
                    g_window.hwnd,
                    Callback<ICoreWebView2CreateCoreWebView2ControllerCompletedHandler>(
                        [&ready, &failed](HRESULT result, ICoreWebView2Controller* controller) -> HRESULT {
                            if (FAILED(result) || !controller) {
                                failed = true;
                                return S_OK;
                            }
                            attach_webview(controller);
                            ready = true;
                            return S_OK;
                        })
                        .Get());
                return S_OK;
            })
            .Get());

    if (FAILED(hr)) {
        Log::error("WebView2 is not available on this system (the Edge WebView2 runtime is missing)");
        MessageBoxW(g_window.hwnd,
                    L"ProjectOreo needs the Microsoft Edge WebView2 runtime, which is missing on this "
                    L"system.\n\nInstall it from:\nhttps://developer.microsoft.com/microsoft-edge/webview2/",
                    L"ProjectOreo", MB_OK | MB_ICONWARNING);
        DestroyWindow(g_window.hwnd);
        return false;
    }

    SetTimer(g_window.hwnd, kRefreshTimer, kRefreshMs, nullptr);
    MSG message;
    while (GetMessageW(&message, nullptr, 0, 0)) {
        TranslateMessage(&message);
        DispatchMessageW(&message);
        if (ready.exchange(false)) g_window.push_state();   // first paint once the page is loaded
        if (failed.exchange(false)) {
            Log::error("WebView2 could not be created");
            break;
        }
        if (g_window.closing && !IsWindow(g_window.hwnd)) break;
    }

    KillTimer(g_window.hwnd, kRefreshTimer);
    const bool played = g_window.played;
    g_window = WindowState();
    return played;
}

}  // namespace oreo
