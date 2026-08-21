cmake_minimum_required(VERSION 3.15)
project(project_oreo LANGUAGES CXX)

# One version number for the whole product: the launcher exe and the loader dll ship together.
set(PROJECTOREO_VERSION "0.1.16")
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
# ProjectOreoLauncher.exe - the mod manager window and every service behind it.
# ---------------------------------------------------------------------------------------
add_executable(ProjectOreoLauncher WIN32
    launcher/src/main.cpp
    launcher/src/launcher.rc
    launcher/src/core/approved_catalog.cpp
    launcher/src/core/app.cpp
    launcher/src/core/component_update.cpp
    launcher/src/core/config.cpp
    launcher/src/core/game_gate.cpp
    launcher/src/core/game_service.cpp
    launcher/src/core/http.cpp
    launcher/src/core/http_download.cpp
    launcher/src/core/launch_service.cpp
    launcher/src/core/loader_service.cpp
    launcher/src/core/log.cpp
    launcher/src/core/mod_discovery.cpp
    launcher/src/core/package_service.cpp
    launcher/src/core/process.cpp
    launcher/src/core/python_runtime.cpp
    launcher/src/core/release_manifest.cpp
    launcher/src/core/settings_service.cpp
    launcher/src/core/state.cpp
    launcher/src/core/update_service.cpp
    launcher/src/core/util.cpp
    launcher/src/core/version.cpp
    launcher/src/ui/image.cpp
    # The window is an HTML page hosted in WebView2, where clicks are the browser's problem.
    launcher/src/ui/ui_v3.cpp
)

set(WEBVIEW2_DIR ${CMAKE_SOURCE_DIR}/third_party/webview2/build/native)

target_include_directories(ProjectOreoLauncher PRIVATE
    launcher/src
    ${GENERATED_DIR}
    ${WEBVIEW2_DIR}/include
)
target_link_directories(ProjectOreoLauncher PRIVATE ${WEBVIEW2_DIR}/x64)

target_compile_definitions(ProjectOreoLauncher PRIVATE
    UNICODE
    _UNICODE
    NOMINMAX
    WIN32_LEAN_AND_MEAN
    PROJECTOREO_VERSION="${PROJECTOREO_VERSION}"
)

# WebView2LoaderStatic.lib is linked statically: no extra DLL ships with the launcher.
target_link_libraries(ProjectOreoLauncher PRIVATE winhttp shlwapi comctl32 version)
target_compile_options(ProjectOreoLauncher PRIVATE /utf-8 /W3)
set_target_properties(ProjectOreoLauncher PROPERTIES OUTPUT_NAME "ProjectOreoLauncher")

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
        launcher/src/core/log.cpp
        launcher/src/core/mod_discovery.cpp
        launcher/src/core/state.cpp
        launcher/src/core/util.cpp
    )
    target_include_directories(mod_discovery_test PRIVATE launcher/src)
    target_compile_definitions(mod_discovery_test PRIVATE UNICODE _UNICODE NOMINMAX WIN32_LEAN_AND_MEAN)
    target_compile_options(mod_discovery_test PRIVATE /utf-8 /W3)
    target_link_libraries(mod_discovery_test PRIVATE version)
    add_test(NAME mod_discovery_test COMMAND mod_discovery_test)

    # What PLAY would tell NMSpy, worked out without starting a game or touching pyMHF.
    add_executable(launch_plan_test
        tests/launch_plan_test.cpp
        launcher/src/core/launch_service.cpp
        launcher/src/core/game_service.cpp
        launcher/src/core/log.cpp
        launcher/src/core/process.cpp
        launcher/src/core/python_runtime.cpp
        launcher/src/core/state.cpp
        launcher/src/core/util.cpp
    )
    target_include_directories(launch_plan_test PRIVATE launcher/src)
    target_compile_definitions(launch_plan_test PRIVATE UNICODE _UNICODE NOMINMAX WIN32_LEAN_AND_MEAN)
    target_compile_options(launch_plan_test PRIVATE /utf-8 /W3)
    target_link_libraries(launch_plan_test PRIVATE version)
    add_test(NAME launch_plan_test COMMAND launch_plan_test)

    # The Settings window's model: validation, saving, and the config.ini writer.
    add_executable(settings_test
        tests/settings_test.cpp
        launcher/src/core/settings_service.cpp
        launcher/src/core/log.cpp
        launcher/src/core/util.cpp
    )
    target_include_directories(settings_test PRIVATE launcher/src)
    target_compile_definitions(settings_test PRIVATE UNICODE _UNICODE NOMINMAX WIN32_LEAN_AND_MEAN)
    target_compile_options(settings_test PRIVATE /utf-8 /W3)
    target_link_libraries(settings_test PRIVATE version)
    add_test(NAME settings_test COMMAND settings_test)

    add_executable(approved_catalog_test
        tests/approved_catalog_test.cpp
        launcher/src/core/approved_catalog.cpp
        launcher/src/core/util.cpp
    )

    # Where updates may come from, and what counts as a valid description of one. Both are
    # decided before anything is downloaded, so both can be checked without a network.
    add_executable(update_source_test
        tests/update_source_test.cpp
        launcher/src/core/approved_catalog.cpp
        launcher/src/core/component_update.cpp
        launcher/src/core/game_service.cpp
        launcher/src/core/http.cpp
        launcher/src/core/process.cpp
        launcher/src/core/python_runtime.cpp
        launcher/src/core/state.cpp
        launcher/src/core/version.cpp
        launcher/src/core/http_download.cpp
        launcher/src/core/log.cpp
        launcher/src/core/release_manifest.cpp
        launcher/src/core/util.cpp
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
endif()
