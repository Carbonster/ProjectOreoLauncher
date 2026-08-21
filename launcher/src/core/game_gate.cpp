#include "game_gate.h"

#include <windows.h>

#include <mutex>
#include <string>

#include "log.h"
#include "util.h"

namespace oreo {

namespace {
std::mutex g_mutex;
unsigned long g_pid = 0;
bool g_released = true;   // no gate at all until gate_init says otherwise
}  // namespace

void gate_init(unsigned long game_pid) {
    std::lock_guard<std::mutex> lock(g_mutex);
    g_pid = game_pid;
    g_released = game_pid == 0;
    if (game_pid) Log::info("game is held at startup by the loader (pid " + std::to_string(game_pid) + ")");
}

void gate_release(const char* reason) {
    std::lock_guard<std::mutex> lock(g_mutex);
    if (g_released) return;
    g_released = true;

    // Same name the loader created; per game process, so two games never cross wires.
    std::wstring name = L"Local\\ProjectOreoGo_" + std::to_wstring(g_pid);
    HANDLE event = OpenEventW(EVENT_MODIFY_STATE, FALSE, name.c_str());
    if (!event) {
        Log::warn("could not reach the loader to release the game (it may have given up waiting)");
        return;
    }
    SetEvent(event);
    CloseHandle(event);
    Log::info(std::string("released the game: ") + reason);
}

void gate_cancel(const char* reason) {
    std::lock_guard<std::mutex> lock(g_mutex);
    if (g_released) return;
    g_released = true;

    std::wstring name = L"Local\\ProjectOreoCancel_" + std::to_wstring(g_pid);
    HANDLE event = OpenEventW(EVENT_MODIFY_STATE, FALSE, name.c_str());
    if (!event) {
        Log::warn("could not reach the loader to cancel the launch");
        return;
    }
    SetEvent(event);
    CloseHandle(event);
    Log::info(std::string("cancelled the launch: ") + reason);
}

bool gate_released() {
    std::lock_guard<std::mutex> lock(g_mutex);
    return g_released;
}

}  // namespace oreo
