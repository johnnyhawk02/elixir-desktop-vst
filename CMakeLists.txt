cmake_minimum_required(VERSION 3.22)
# Elixir Desktop VST3: its own project. JUCE and johnny_add_plugin() come from the Plugins workspace
# (~/Developer/Plugins), which is only read; the build goes in ./build.
project(ElixirDesktopVST VERSION 1.0.0)

set(CMAKE_OSX_DEPLOYMENT_TARGET "11.0" CACHE STRING "")
set(CMAKE_OSX_ARCHITECTURES "arm64" CACHE STRING "")
set(CMAKE_CXX_STANDARD 17)

set(JOHNNY_PLUGINS "$ENV{HOME}/Developer/Plugins" CACHE PATH "the Plugins workspace (JUCE, cmake/JohnnyPlugin.cmake)")
add_subdirectory("${JOHNNY_PLUGINS}/JUCE" "${CMAKE_BINARY_DIR}/JUCE")
include("${JOHNNY_PLUGINS}/cmake/JohnnyPlugin.cmake")

# Elixir Desktop: the app's LXR engine, sequencer and FX ported to C++ (Source/Lxr*, Player*), with the
# app's own page as the UI in a web view (web/index.html, built from ~/Developer/elixir-desktop by tools/build-web.mjs).
juce_add_binary_data(ElixirDesktopWeb SOURCES web/index.html)

johnny_add_plugin(ElixirDesktop
    NAME "Elixir Desktop"
    CODE Elxd
    TYPE instrument
    VERSION 1.0.0
    CATEGORIES Instrument Drum
    WANTS_KEYBOARD_FOCUS
    WEB_BROWSER
    LIBRARIES ElixirDesktopWeb
    SOURCES
        Source/Lxr.cpp
        Source/Player.cpp
        Source/Msg.cpp
        Source/Storage.cpp
        Source/PluginProcessor.cpp
        Source/WebHost.cpp
        Source/PluginEditor.cpp
        Source/WebDialogs.mm)

# the engine relies on JS-exact float rounding: no fused multiply-adds; and it compares floats exactly where the JS does
set_source_files_properties(Source/Lxr.cpp Source/Player.cpp PROPERTIES COMPILE_OPTIONS "-ffp-contract=off;-Wno-float-equal")
set_source_files_properties(Source/Msg.cpp PROPERTIES COMPILE_OPTIONS "-Wno-float-equal")

# ElixirDesktop_Compare: runs the C++ player on a message script, for tools/compare.mjs (JS vs C++, sample for sample)
juce_add_console_app(ElixirDesktop_Compare PRODUCT_NAME "ElixirDesktop_Compare")
set_target_properties(ElixirDesktop_Compare PROPERTIES EXCLUDE_FROM_ALL TRUE)
target_sources(ElixirDesktop_Compare PRIVATE tools/compare/Main.cpp Source/Lxr.cpp Source/Player.cpp Source/Msg.cpp)
target_include_directories(ElixirDesktop_Compare PRIVATE Source)
target_compile_definitions(ElixirDesktop_Compare PRIVATE JUCE_WEB_BROWSER=0 JUCE_USE_CURL=0)
target_link_libraries(ElixirDesktop_Compare PRIVATE juce::juce_core juce::juce_recommended_config_flags)

# vst3render (the Plugins workspace's offline renderer for the installed VST3), built here: it looks for
# plugins/<Folder>/plugin.json under JOHNNY_ROOT, so build/render-root/plugins/ElixirDesktop points back at this folder.
#   build/vst3render_artefacts/Release/vst3render ElixirDesktop --bpm 120 --notes none [--state f]
file(MAKE_DIRECTORY "${CMAKE_BINARY_DIR}/render-root/plugins")
file(CREATE_LINK "${CMAKE_SOURCE_DIR}" "${CMAKE_BINARY_DIR}/render-root/plugins/ElixirDesktop" SYMBOLIC)
juce_add_console_app(vst3render PRODUCT_NAME "vst3render")
set_target_properties(vst3render PROPERTIES EXCLUDE_FROM_ALL TRUE)
target_sources(vst3render PRIVATE "${JOHNNY_PLUGINS}/tools/vst3render/Main.cpp")
target_compile_definitions(vst3render PRIVATE JOHNNY_ROOT="${CMAKE_BINARY_DIR}/render-root"
    JUCE_PLUGINHOST_VST3=1 JUCE_PLUGINHOST_AU=0 JUCE_WEB_BROWSER=0 JUCE_USE_CURL=0)
target_link_libraries(vst3render PRIVATE juce::juce_audio_processors juce::juce_audio_formats juce::juce_recommended_config_flags)
