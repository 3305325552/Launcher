cmake_minimum_required(VERSION 3.24)

project(Launcher VERSION 0.9.24 LANGUAGES C CXX RC)

add_compile_definitions(LAUNCHER_VERSION="${PROJECT_VERSION}")

set(CMAKE_CXX_STANDARD 20)
set(CMAKE_CXX_STANDARD_REQUIRED ON)
set(CMAKE_CXX_EXTENSIONS OFF)
option(LAUNCHER_STATIC_RUNTIME "Statically link the compiler runtime for portable release packages" OFF)

if(LAUNCHER_STATIC_RUNTIME AND MSVC)
    set(CMAKE_MSVC_RUNTIME_LIBRARY "MultiThreaded$<$<CONFIG:Debug>:Debug>")
endif()

if(MSVC)
    add_compile_options(/utf-8)
endif()

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

add_library(launcher_core STATIC
    src/app/AppContext.cpp
    src/core/AnimatedBackground.cpp
    src/core/ConfigStore.cpp
    src/core/GlobalFileIndex.cpp
    src/core/LaunchParameterUtils.cpp
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

set(LAUNCHER_UI_COMMON_SOURCES
    src/ui/common/Localization.cpp
    src/ui/common/MaterialIconRegistry.cpp
    src/ui/common/UiAnimation.cpp
    src/ui/common/UiChrome.cpp
    src/ui/common/UiTheme.cpp
)

set(LAUNCHER_UI_PLATFORM_SOURCES
    src/ui/platform/UiPlatform.cpp
)

set(LAUNCHER_UI_RENDERING_SOURCES
    src/ui/rendering/MainDockResources.cpp
)

set(LAUNCHER_UI_NOTES_SOURCES
    src/ui/notes/NoteImageService.cpp
)

set(LAUNCHER_UI_DOCK_SOURCES
    src/ui/dock/MainDock.cpp
    src/ui/dock/MainDockCategoryRail.cpp
    src/ui/dock/MainDockContextMenus.cpp
    src/ui/dock/MainDockDialogs.cpp
    src/ui/dock/MainDockGrid.cpp
    src/ui/dock/MainDockItemEditor.cpp
    src/ui/dock/MainDockItemViews.cpp
    src/ui/dock/MainDockMenu.cpp
    src/ui/dock/MainDockNavigation.cpp
    src/ui/dock/MainDockNotes.cpp
    src/ui/dock/MainDockSearch.cpp
    src/ui/dock/MainDockState.cpp
)

set(LAUNCHER_UI_SETTINGS_SOURCES
    src/ui/settings/ConfigTransfer.cpp
    src/ui/settings/SettingsPages.cpp
    src/ui/settings/SettingsPanel.cpp
    src/ui/settings/SettingsWidgets.cpp
    src/ui/settings/ThemeEditor.cpp
)

set(LAUNCHER_UI_VIEW_SOURCES
    src/ui/views/MarkdownView.cpp
    src/ui/views/UserGuideView.cpp
)

add_library(launcher_ui STATIC
    ${LAUNCHER_UI_COMMON_SOURCES}
    ${LAUNCHER_UI_PLATFORM_SOURCES}
    ${LAUNCHER_UI_RENDERING_SOURCES}
    ${LAUNCHER_UI_NOTES_SOURCES}
    ${LAUNCHER_UI_DOCK_SOURCES}
    ${LAUNCHER_UI_SETTINGS_SOURCES}
    ${LAUNCHER_UI_VIEW_SOURCES}
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

add_executable(launcher_ui_logic_tests
    tests/MainDockNavigationTests.cpp
    src/ui/dock/MainDockNavigation.cpp
)

target_link_libraries(launcher_core_tests PRIVATE
    launcher_core
)

target_include_directories(launcher_ui_logic_tests PRIVATE
    ${CMAKE_CURRENT_SOURCE_DIR}/include
    ${CMAKE_CURRENT_SOURCE_DIR}/src
)

add_test(NAME launcher_core_tests COMMAND launcher_core_tests)
add_test(NAME launcher_ui_logic_tests COMMAND launcher_ui_logic_tests)

add_test(
    NAME launcher_ui_dependency_tests
    COMMAND ${CMAKE_COMMAND}
            -DSOURCE_ROOT=${CMAKE_CURRENT_SOURCE_DIR}
            -P ${CMAKE_CURRENT_SOURCE_DIR}/tests/UiDependencyTests.cmake
)

if(LAUNCHER_STATIC_RUNTIME AND MINGW)
    foreach(target_name
            launcher
            launcher_updater
            launcher_sample_echo_plugin
            launcher_sample_toolbox_plugin
            launcher_sample_native_plugin
            launcher_core_tests
            launcher_ui_logic_tests)
        target_link_options(${target_name} PRIVATE -static-libgcc -static-libstdc++)
    endforeach()
endif()

if(MSVC)
    foreach(target_name
            launcher_core
            launcher_platform_win32
            launcher_ui
            launcher
            launcher_updater
            launcher_sample_echo_plugin
            launcher_sample_toolbox_plugin
            launcher_sample_native_plugin
            launcher_core_tests
            launcher_ui_logic_tests)
        set_property(TARGET ${target_name} PROPERTY INTERPROCEDURAL_OPTIMIZATION_RELEASE TRUE)
    endforeach()
endif()
