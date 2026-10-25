// window_state.h - everything the launcher window holds while it is open.
#pragma once

#include <windows.h>
#include <wrl/client.h>

#include <string>

#include "app/app.h"
#include "ui/card_order.h"
#include "ui/thumbnails.h"
#include "WebView2.h"

namespace oreo {

struct WindowState {
    App* app = nullptr;
    HWND hwnd = nullptr;
    Microsoft::WRL::ComPtr<ICoreWebView2Controller> controller;
    Microsoft::WRL::ComPtr<ICoreWebView2> webview;
    // What the window paints until the page has drawn: the page's own background.
    COLORREF frame_color = RGB(0x0b, 0x0b, 0x0b);

    bool played = false;    // PLAY or PLAY WITHOUT MODS was pressed
    bool closing = false;
    std::string last_state;   // the state the page last drew; cleared to force a repaint
    CardOrder cards;
    Thumbnails thumbnails;

    // Runs one statement in the page. Everything the window tells the page goes through here.
    void call_page(const std::string& javascript) const;
    // Sends the page the current state, when it changed.
    void push_state();
    void repaint_all() { last_state.clear(); }
};

}  // namespace oreo
