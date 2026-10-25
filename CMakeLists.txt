cmake_minimum_required(VERSION 3.15)
project(project_oreo LANGUAGES CXX)

# One version number for the whole product: the launcher exe and the loader dll ship together.
set(PROJECTOREO_VERSION "0.2.1")
string(REPLACE "." "," PROJECTOREO_VERSION_COMMA "${PROJECTOREO_VERSION}")
set(PROJECTOREO_VERSION_COMMA "${PROJECTOREO_VERSION_COMMA},0")

set(CMAKE_CXX_STANDARD 17)
set(CMAKE_CXX_STANDARD_REQUIRED ON)

# No Man's Sky is 64-bit, and the loader is loaded inside it. Configure with -A x64.
if(NOT CMAKE_SIZEOF_VOID_P EQUAL 8)
    message(FATAL_ERROR "Must build 64-bit. Configure with:  cmake -S . -B build -A x64")
endif()

configure_file(cmake/oreo_version.h.in ${CMAKE_BINARY_DIR}/generated/oreo_version.h @ONLY)
set(GENERATED_DIR ${CMAKE_BINARY_DIR}/generated)

# ---------------------------------------------------------------------------------------
# OreoLauncher.exe - the mod manager window and every service behind it.
# ---------------------------------------------------------------------------------------
add_executable(OreoLauncher WIN32
    launcher/src/main.cpp
    launcher/src/launcher.rc
    launcher/src/updates/approved_catalog.cpp
    launcher/src/app/app.cpp
    launcher/src/app/checks.cpp
    launcher/src/app/component_status.cpp
    launcher/src/app/mod_readiness.cpp
    launcher/src/app/operations.cpp
    launcher/src/app/play.cpp
    launcher/src/app/preferences.cpp
    launcher/src/app/window_rule.cpp
    launcher/src/app/test_mode.cpp
    launcher/src/updates/component_update.cpp
    launcher/src/updates/mod_files.cpp
    launcher/src/settings/config.cpp
    launcher/src/game/game_gate.cpp
    launcher/src/game/game_service.cpp
    launcher/src/net/http.cpp
    launcher/src/net/http_download.cpp
    launcher/src/game/launch_service.cpp
    launcher/src/game/loader_service.cpp
    launcher/src/system/log.cpp
    launcher/src/mods/mod_discovery.cpp
    launcher/src/python/package_service.cpp
    launcher/src/system/process.cpp
    launcher/src/python/python_runtime.cpp
    launcher/src/updates/release_manifest.cpp
    launcher/src/settings/settings_service.cpp
    launcher/src/model/state.cpp
    launcher/src/updates/update_service.cpp
    launcher/src/system/json.cpp
    launcher/src/system/text.cpp
    launcher/src/system/files.cpp
    launcher/src/system/ini.cpp
    launcher/src/system/clock.cpp
    launcher/src/updates/version.cpp
    # The window is an HTML page hosted in WebView2, where clicks are the browser's problem.
    launcher/src/ui/card_order.cpp
    launcher/src/ui/component_texts.cpp
    launcher/src/ui/folder_picker.cpp
    launcher/src/ui/page.cpp
    launcher/src/ui/page_commands.cpp
    launcher/src/ui/thumbnails.cpp
    launcher/src/ui/view_state.cpp
    launcher/src/ui/window.cpp
)
# The page is built into the executable as resources; a change to it has to rebuild them.
file(GLOB PAGE_FILES CONFIGURE_DEPENDS ${CMAKE_SOURCE_DIR}/launcher/src/ui/page/*)
set_source_files_properties(launcher/src/launcher.rc PROPERTIES OBJECT_DEPENDS "${PAGE_FILES}")

set(WEBVIEW2_DIR ${CMAKE_SOURCE_DIR}/third_party/webview2/build/native)

target_include_directories(OreoLauncher PRIVATE
    launcher/src
    ${GENERATED_DIR}
    ${WEBVIEW2_DIR}/include
)
target_link_directories(OreoLauncher PRIVATE ${WEBVIEW2_DIR}/x64)

target_compile_definitions(OreoLauncher PRIVATE
    UNICODE
    _UNICODE
    NOMINMAX
    WIN32_LEAN_AND_MEAN
    PROJECTOREO_VERSION="${PROJECTOREO_VERSION}"
)

# WebView2LoaderStatic.lib is linked statically: no extra DLL ships with the launcher.
target_link_libraries(OreoLauncher PRIVATE winhttp shlwapi comctl32 version)
target_compile_options(OreoLauncher PRIVATE /utf-8 /W3)
set_target_properties(OreoLauncher PROPERTIES OUTPUT_NAME "OreoLauncher")

# ---------------------------------------------------------------------------------------
# version.dll - the loader the game itself picks up from its Binaries folder.
# ---------------------------------------------------------------------------------------
add_library(oreo_loader SHARED
    loader/src/loader.cpp
    loader/src/loader.def
    loader/src/loader.rc
)
target_include_directories(oreo_loader PRIVATE ${GENERATED_DIR})
target_compile_definitions(oreo_loader PRIVATE UNICODE _UNICODE NOMINMAX WIN32_LEAN_AND_MEAN)
target_compile_options(oreo_loader PRIVATE /utf-8 /W3)
# The file must literally be called version.dll: that is the name NMS.exe imports.
set_target_properties(oreo_loader PROPERTIES OUTPUT_NAME "version" PREFIX "")

include(CTest)
if(BUILD_TESTING)
    add_executable(mod_discovery_test
        tests/mod_discovery_test.cpp
        launcher/src/system/log.cpp
        launcher/src/mods/mod_discovery.cpp
        launcher/src/model/state.cpp
        launcher/src/system/json.cpp
    launcher/src/system/text.cpp
        launcher/src/system/files.cpp
        launcher/src/system/ini.cpp
        launcher/src/system/clock.cpp
    )
    target_include_directories(mod_discovery_test PRIVATE launcher/src)
    target_compile_definitions(mod_discovery_test PRIVATE UNICODE _UNICODE NOMINMAX WIN32_LEAN_AND_MEAN)
    target_compile_options(mod_discovery_test PRIVATE /utf-8 /W3)
    target_link_libraries(mod_discovery_test PRIVATE version)
    add_test(NAME mod_discovery_test COMMAND mod_discovery_test)

    # What PLAY would tell NMSpy, worked out without starting a game or touching pyMHF.
    add_executable(launch_plan_test
        tests/launch_plan_test.cpp
        launcher/src/game/launch_service.cpp
        launcher/src/game/game_service.cpp
        launcher/src/system/log.cpp
        launcher/src/system/process.cpp
        launcher/src/python/python_runtime.cpp
        launcher/src/model/state.cpp
        launcher/src/system/json.cpp
    launcher/src/system/text.cpp
        launcher/src/system/files.cpp
        launcher/src/system/ini.cpp
        launcher/src/system/clock.cpp
    )
    target_include_directories(launch_plan_test PRIVATE launcher/src)
    target_compile_definitions(launch_plan_test PRIVATE UNICODE _UNICODE NOMINMAX WIN32_LEAN_AND_MEAN)
    target_compile_options(launch_plan_test PRIVATE /utf-8 /W3)
    target_link_libraries(launch_plan_test PRIVATE version)
    add_test(NAME launch_plan_test COMMAND launch_plan_test)

    # The Settings window's model: validation, saving, and the config.ini writer.
    add_executable(settings_test
        tests/settings_test.cpp
        launcher/src/settings/settings_service.cpp
        launcher/src/system/log.cpp
        launcher/src/system/json.cpp
    launcher/src/system/text.cpp
        launcher/src/system/files.cpp
        launcher/src/system/ini.cpp
        launcher/src/system/clock.cpp
    )
    target_include_directories(settings_test PRIVATE launcher/src)
    target_compile_definitions(settings_test PRIVATE UNICODE _UNICODE NOMINMAX WIN32_LEAN_AND_MEAN)
    target_compile_options(settings_test PRIVATE /utf-8 /W3)
    target_link_libraries(settings_test PRIVATE version)
    add_test(NAME settings_test COMMAND settings_test)

    add_executable(approved_catalog_test
        tests/approved_catalog_test.cpp
        launcher/src/updates/approved_catalog.cpp
        launcher/src/system/json.cpp
    launcher/src/system/text.cpp
        launcher/src/system/files.cpp
        launcher/src/system/ini.cpp
        launcher/src/system/clock.cpp
    )

    # Where updates may come from, and what counts as a valid description of one. Both are
    # decided before anything is downloaded, so both can be checked without a network.
    add_executable(update_source_test
        tests/update_source_test.cpp
        launcher/src/updates/approved_catalog.cpp
        launcher/src/updates/component_update.cpp
        launcher/src/updates/mod_files.cpp
        launcher/src/game/game_service.cpp
        launcher/src/net/http.cpp
        launcher/src/system/process.cpp
        launcher/src/python/python_runtime.cpp
        launcher/src/model/state.cpp
        launcher/src/updates/version.cpp
        launcher/src/net/http_download.cpp
        launcher/src/system/log.cpp
        launcher/src/updates/release_manifest.cpp
        launcher/src/system/json.cpp
    launcher/src/system/text.cpp
        launcher/src/system/files.cpp
        launcher/src/system/ini.cpp
        launcher/src/system/clock.cpp
    )
    target_include_directories(update_source_test PRIVATE launcher/src)
    target_compile_definitions(update_source_test PRIVATE UNICODE _UNICODE NOMINMAX WIN32_LEAN_AND_MEAN)
    target_compile_options(update_source_test PRIVATE /utf-8 /W3)
    target_link_libraries(update_source_test PRIVATE winhttp version)
    add_test(NAME update_source_test COMMAND update_source_test)
    target_include_directories(approved_catalog_test PRIVATE launcher/src)
    target_compile_definitions(approved_catalog_test PRIVATE
        UNICODE _UNICODE NOMINMAX WIN32_LEAN_AND_MEAN
    )
    target_compile_options(approved_catalog_test PRIVATE /utf-8 /W3)
    target_link_libraries(approved_catalog_test PRIVATE version)
    add_test(NAME approved_catalog_test COMMAND approved_catalog_test)

    add_executable(mod_files_test
        tests/mod_files_test.cpp
        launcher/src/updates/mod_files.cpp
        launcher/src/system/log.cpp
        launcher/src/system/text.cpp
        launcher/src/system/files.cpp
        launcher/src/system/clock.cpp
    )
    target_include_directories(mod_files_test PRIVATE launcher/src)
    target_compile_definitions(mod_files_test PRIVATE UNICODE _UNICODE NOMINMAX WIN32_LEAN_AND_MEAN)
    target_compile_options(mod_files_test PRIVATE /utf-8 /W3)
    target_link_libraries(mod_files_test PRIVATE version)
    add_test(NAME mod_files_test COMMAND mod_files_test)

    # What the launcher decides from what it found: component states, card buttons and order,
    # and whether the window opens. Plain functions, so no window, game or network is needed.
    add_executable(decisions_test
        tests/decisions_test.cpp
        launcher/src/app/component_status.cpp
        launcher/src/app/mod_readiness.cpp
        launcher/src/app/window_rule.cpp
        launcher/src/model/state.cpp
        launcher/src/system/clock.cpp
        launcher/src/system/files.cpp
        launcher/src/system/log.cpp
        launcher/src/system/text.cpp
        launcher/src/ui/card_order.cpp
        launcher/src/updates/version.cpp
    )
    target_include_directories(decisions_test PRIVATE launcher/src)
    target_compile_definitions(decisions_test PRIVATE UNICODE _UNICODE NOMINMAX WIN32_LEAN_AND_MEAN)
    target_compile_options(decisions_test PRIVATE /utf-8 /W3)
    target_link_libraries(decisions_test PRIVATE version)
    add_test(NAME decisions_test COMMAND decisions_test)

    # The page as the window loads it from the executable's resources, and the JSON both ways.
    add_executable(page_test
        tests/page_test.cpp
        launcher/src/launcher.rc
        launcher/src/system/clock.cpp
        launcher/src/system/files.cpp
        launcher/src/system/json.cpp
        launcher/src/system/log.cpp
        launcher/src/system/text.cpp
        launcher/src/ui/page.cpp
    )
    target_include_directories(page_test PRIVATE launcher/src ${GENERATED_DIR})
    target_compile_definitions(page_test PRIVATE UNICODE _UNICODE NOMINMAX WIN32_LEAN_AND_MEAN)
    target_compile_options(page_test PRIVATE /utf-8 /W3)
    target_link_libraries(page_test PRIVATE version)
    add_test(NAME page_test COMMAND page_test)
endif()
