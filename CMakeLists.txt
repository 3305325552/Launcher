cmake_minimum_required(VERSION 3.24)

project(Launcher VERSION 0.9.23 LANGUAGES C CXX RC)

add_compile_definitions(LAUNCHER_VERSION="${PROJECT_VERSION}")

set(CMAKE_CXX_STANDARD 20)
set(CMAKE_CXX_STANDARD_REQUIRED ON)
set(CMAKE_CXX_EXTENSIONS OFF)
option(LAUNCHER_STATIC_RUNTIME "Statically link MinGW runtime libraries for portable release packages" OFF)
set(LAUNCHER_APP_ICON "${CMAKE_CURRENT_SOURCE_DIR}/assets/icon/icon.ico")

include(FetchContent)

FetchContent_Declare(
    nlohmann_json
    GIT_REPOSITORY https://github.com/nlohmann/json.git
    GIT_TAG v3.12.0
)
FetchContent_MakeAvailable(nlohmann_json)

add_subdirectory(third_party/imgui)

FetchContent_Declare(
    md4c
    GIT_REPOSITORY https://github.com/mity/md4c.git
    GIT_TAG release-0.5.2
)
set(BUILD_MD2HTML_EXECUTABLE OFF CACHE BOOL "" FORCE)
FetchContent_MakeAvailable(md4c)

FetchContent_Declare(
    cpr
    GIT_REPOSITORY https://github.com/libcpr/cpr.git
    GIT_TAG 1.12.0
)
set(CPR_BUILD_TESTS OFF CACHE BOOL "" FORCE)
set(CPR_USE_SYSTEM_CURL OFF CACHE BOOL "" FORCE)
set(CPR_USE_SYSTEM_LIB_PSL ON CACHE BOOL "" FORCE)
set(CURL_USE_LIBPSL OFF CACHE BOOL "" FORCE)
FetchContent_MakeAvailable(cpr)

add_library(launcher_core STATIC
    src/app/AppContext.cpp
    src/core/AnimatedBackground.cpp
    src/core/ConfigStore.cpp
    src/core/GlobalFileIndex.cpp
    src/core/LauncherService.cpp
    src/core/LocalVolumeFileProvider.cpp
    src/core/ModelActions.cpp
    src/core/NotesStore.cpp
    src/core/PluginManager.cpp
    src/core/SearchIndex.cpp
    src/core/ThemeStore.cpp
    src/update/UpdateService.cpp
    src/data/SeedData.cpp
)

target_include_directories(launcher_core PUBLIC
    ${CMAKE_CURRENT_SOURCE_DIR}/include
    ${CMAKE_CURRENT_SOURCE_DIR}/src
)

target_link_libraries(launcher_core PUBLIC
    nlohmann_json::nlohmann_json
    cpr::cpr
    shell32
    ole32
    winhttp
    bcrypt
)

target_compile_definitions(launcher_core PUBLIC
    NOMINMAX
    WIN32_LEAN_AND_MEAN
)

add_library(launcher_platform_win32 STATIC
    src/platform/SystemIntegration.cpp
    src/platform/Win32Window.cpp
)

target_include_directories(launcher_platform_win32 PUBLIC
    ${CMAKE_CURRENT_SOURCE_DIR}/include
    ${CMAKE_CURRENT_SOURCE_DIR}/src
)

target_link_libraries(launcher_platform_win32 PUBLIC
    CustomImGui::custom_imgui
    d3d11
    d3dcompiler
    dxgi
    dwmapi
    comctl32
    shell32
    wtsapi32
)

target_compile_definitions(launcher_platform_win32 PUBLIC
    NOMINMAX
    WIN32_LEAN_AND_MEAN
)

add_library(launcher_ui STATIC
    src/ui/ConfigTransfer.cpp
    src/ui/MainDockCategoryRail.cpp
    src/ui/MainDock.cpp
    src/ui/MainDockChrome.cpp
    src/ui/MainDockContextMenus.cpp
    src/ui/MainDockDialogs.cpp
    src/ui/MainDockGrid.cpp
    src/ui/MainDockItemEditor.cpp
    src/ui/MainDockItemViews.cpp
    src/ui/MainDockMenu.cpp
    src/ui/MainDockNotes.cpp
    src/ui/MaterialIconRegistry.cpp
    src/ui/MarkdownView.cpp
    src/ui/MainDockSearch.cpp
    src/ui/MainDockState.cpp
    src/ui/MainDockWin32.cpp
    src/ui/Localization.cpp
    src/ui/SettingsPanel.cpp
    src/ui/ThemeEditor.cpp
    src/ui/UiAnimation.cpp
    src/ui/UiTheme.cpp
    src/ui/UserGuideView.cpp
)

target_include_directories(launcher_ui PUBLIC
    ${CMAKE_CURRENT_SOURCE_DIR}/include
    ${CMAKE_CURRENT_SOURCE_DIR}/src
    ${md4c_SOURCE_DIR}/src
)

target_link_libraries(launcher_ui PUBLIC
    launcher_core
    launcher_platform_win32
    CustomImGui::custom_imgui
    md4c
    windowscodecs
    comdlg32
    shell32
    ole32
    uuid
    dwmapi
    comctl32
)

target_compile_definitions(launcher_ui PUBLIC
    NOMINMAX
    WIN32_LEAN_AND_MEAN
)

add_executable(launcher WIN32
    src/main.cpp
    src/app/Application.cpp
    src/resources/app.rc
)

add_executable(launcher_updater WIN32
    src/update/UpdaterMain.cpp
)

target_link_libraries(launcher_updater PRIVATE
    shell32
)

target_compile_definitions(launcher_updater PRIVATE
    NOMINMAX
    WIN32_LEAN_AND_MEAN
)

set_property(SOURCE src/resources/app.rc APPEND PROPERTY OBJECT_DEPENDS "${LAUNCHER_APP_ICON}")

target_include_directories(launcher PRIVATE
    ${CMAKE_CURRENT_SOURCE_DIR}/include
    ${CMAKE_CURRENT_SOURCE_DIR}/src
)

target_link_libraries(launcher PRIVATE
    launcher_core
    launcher_platform_win32
    launcher_ui
    CustomImGui::custom_imgui
    d3d11
    d3dcompiler
    dxgi
    dwmapi
    windowscodecs
    comctl32
)

target_compile_definitions(launcher PRIVATE
    NOMINMAX
    WIN32_LEAN_AND_MEAN
)

add_custom_target(launcher-assets ALL
    COMMAND ${CMAKE_COMMAND} -E make_directory $<TARGET_FILE_DIR:launcher>/assets
    COMMAND ${CMAKE_COMMAND} -E make_directory $<TARGET_FILE_DIR:launcher>/assets/docs
    COMMAND ${CMAKE_COMMAND} -E copy_directory
            ${CMAKE_CURRENT_SOURCE_DIR}/assets
            $<TARGET_FILE_DIR:launcher>/assets
    COMMAND ${CMAKE_COMMAND} -E copy_if_different
            ${CMAKE_CURRENT_SOURCE_DIR}/docs/USER_GUIDE.md
            $<TARGET_FILE_DIR:launcher>/assets/docs/USER_GUIDE.md
    COMMAND ${CMAKE_COMMAND} -E copy_if_different
            ${CMAKE_CURRENT_SOURCE_DIR}/docs/PLUGIN_SYSTEM.md
            $<TARGET_FILE_DIR:launcher>/assets/docs/PLUGIN_SYSTEM.md
    COMMAND ${CMAKE_COMMAND} -E copy_if_different
            ${CMAKE_CURRENT_SOURCE_DIR}/docs/PLUGIN_API.md
            $<TARGET_FILE_DIR:launcher>/assets/docs/PLUGIN_API.md
    COMMENT "Syncing Launcher assets"
)

add_dependencies(launcher launcher-assets)

add_executable(launcher_sample_echo_plugin
    tests/plugins/sample_echo/SampleEchoPlugin.cpp
)

add_executable(launcher_sample_toolbox_plugin
    tests/plugins/sample_toolbox/SampleToolboxPlugin.cpp
)

add_library(launcher_sample_native_plugin SHARED
    tests/plugins/sample_native/SampleNativePlugin.cpp
)

target_link_libraries(launcher_sample_echo_plugin PRIVATE
    nlohmann_json::nlohmann_json
    user32
)

target_link_libraries(launcher_sample_toolbox_plugin PRIVATE
    nlohmann_json::nlohmann_json
    user32
)

target_link_libraries(launcher_sample_native_plugin PRIVATE
    nlohmann_json::nlohmann_json
)

target_compile_definitions(launcher_sample_echo_plugin PRIVATE
    NOMINMAX
    WIN32_LEAN_AND_MEAN
)

target_compile_definitions(launcher_sample_toolbox_plugin PRIVATE
    NOMINMAX
    WIN32_LEAN_AND_MEAN
)

target_compile_definitions(launcher_sample_native_plugin PRIVATE
    NOMINMAX
    WIN32_LEAN_AND_MEAN
)

target_include_directories(launcher_sample_native_plugin PRIVATE
    ${CMAKE_CURRENT_SOURCE_DIR}/include
)

set_target_properties(launcher_sample_echo_plugin PROPERTIES
    RUNTIME_OUTPUT_DIRECTORY "${CMAKE_BINARY_DIR}/sample-plugins"
)

set_target_properties(launcher_sample_toolbox_plugin PROPERTIES
    RUNTIME_OUTPUT_DIRECTORY "${CMAKE_BINARY_DIR}/sample-plugins"
)

set_target_properties(launcher_sample_native_plugin PROPERTIES
    RUNTIME_OUTPUT_DIRECTORY "${CMAKE_BINARY_DIR}/sample-plugins"
    LIBRARY_OUTPUT_DIRECTORY "${CMAKE_BINARY_DIR}/sample-plugins"
    ARCHIVE_OUTPUT_DIRECTORY "${CMAKE_BINARY_DIR}/sample-plugins"
    PREFIX ""
)

add_custom_target(launcher-sample-plugins ALL
    COMMAND ${CMAKE_COMMAND} -E make_directory $<TARGET_FILE_DIR:launcher>/plugins/sample-echo
    COMMAND ${CMAKE_COMMAND} -E make_directory $<TARGET_FILE_DIR:launcher>/plugins/sample-calc
    COMMAND ${CMAKE_COMMAND} -E make_directory $<TARGET_FILE_DIR:launcher>/plugins/sample-color
    COMMAND ${CMAKE_COMMAND} -E make_directory $<TARGET_FILE_DIR:launcher>/plugins/sample-cli
    COMMAND ${CMAKE_COMMAND} -E make_directory $<TARGET_FILE_DIR:launcher>/plugins/sample-native
    COMMAND ${CMAKE_COMMAND} -E copy_if_different
            $<TARGET_FILE:launcher_sample_echo_plugin>
            $<TARGET_FILE_DIR:launcher>/plugins/sample-echo/$<TARGET_FILE_NAME:launcher_sample_echo_plugin>
    COMMAND ${CMAKE_COMMAND} -E copy_if_different
            $<TARGET_FILE:launcher_sample_toolbox_plugin>
            $<TARGET_FILE_DIR:launcher>/plugins/sample-calc/$<TARGET_FILE_NAME:launcher_sample_toolbox_plugin>
    COMMAND ${CMAKE_COMMAND} -E copy_if_different
            $<TARGET_FILE:launcher_sample_toolbox_plugin>
            $<TARGET_FILE_DIR:launcher>/plugins/sample-color/$<TARGET_FILE_NAME:launcher_sample_toolbox_plugin>
    COMMAND ${CMAKE_COMMAND} -E copy_if_different
            $<TARGET_FILE:launcher_sample_toolbox_plugin>
            $<TARGET_FILE_DIR:launcher>/plugins/sample-cli/$<TARGET_FILE_NAME:launcher_sample_toolbox_plugin>
    COMMAND ${CMAKE_COMMAND} -E copy_if_different
            $<TARGET_FILE:launcher_sample_native_plugin>
            $<TARGET_FILE_DIR:launcher>/plugins/sample-native/$<TARGET_FILE_NAME:launcher_sample_native_plugin>
    COMMAND ${CMAKE_COMMAND} -E copy_if_different
            ${CMAKE_CURRENT_SOURCE_DIR}/tests/plugins/sample_echo/plugin.json
            $<TARGET_FILE_DIR:launcher>/plugins/sample-echo/plugin.json
    COMMAND ${CMAKE_COMMAND} -E copy_if_different
            ${CMAKE_CURRENT_SOURCE_DIR}/tests/plugins/sample_calc/plugin.json
            $<TARGET_FILE_DIR:launcher>/plugins/sample-calc/plugin.json
    COMMAND ${CMAKE_COMMAND} -E copy_if_different
            ${CMAKE_CURRENT_SOURCE_DIR}/tests/plugins/sample_color/plugin.json
            $<TARGET_FILE_DIR:launcher>/plugins/sample-color/plugin.json
    COMMAND ${CMAKE_COMMAND} -E copy_if_different
            ${CMAKE_CURRENT_SOURCE_DIR}/tests/plugins/sample_cli/plugin.json
            $<TARGET_FILE_DIR:launcher>/plugins/sample-cli/plugin.json
    COMMAND ${CMAKE_COMMAND} -E copy_if_different
            ${CMAKE_CURRENT_SOURCE_DIR}/tests/plugins/sample_native/plugin.json
            $<TARGET_FILE_DIR:launcher>/plugins/sample-native/plugin.json
    DEPENDS launcher_sample_echo_plugin launcher_sample_toolbox_plugin launcher_sample_native_plugin
    COMMENT "Packaging sample plugins"
)

enable_testing()

add_executable(launcher_core_tests
    tests/SearchIndexTests.cpp
)

target_link_libraries(launcher_core_tests PRIVATE
    launcher_core
)

add_test(NAME launcher_core_tests COMMAND launcher_core_tests)

if(LAUNCHER_STATIC_RUNTIME AND MINGW)
    foreach(target_name
            launcher
            launcher_updater
            launcher_sample_echo_plugin
            launcher_sample_toolbox_plugin
            launcher_sample_native_plugin
            launcher_core_tests)
        target_link_options(${target_name} PRIVATE -static)
    endforeach()
endif()
