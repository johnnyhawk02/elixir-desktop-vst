cmake_minimum_required(VERSION 3.22)
# Elixir Desktop: the app's LXR engine, sequencer and FX ported to C++ (Source/Lxr*, Player*), with the
# app's own page as the UI in a web view (web/index.html, built from ~/Developer/elixir-desktop by tools/build-web.mjs).
# Builds on its own: JUCE and the CLAP wrapper are fetched at the versions below. To save the download on Johnny's Mac:
#   cmake -B build -DCMAKE_BUILD_TYPE=Release -DFETCHCONTENT_SOURCE_DIR_JUCE=$HOME/Developer/Plugins/JUCE
set(CMAKE_OSX_DEPLOYMENT_TARGET "11.0" CACHE STRING "")
set(CMAKE_OSX_ARCHITECTURES "arm64;x86_64" CACHE STRING "") # Apple Silicon and Intel
if(WIN32)
    set(CMAKE_MSVC_RUNTIME_LIBRARY "MultiThreaded$<$<CONFIG:Debug>:Debug>") # no Visual C++ runtime needed on the player's PC
endif()
project(ElixirDesktop VERSION 1.1.0 LANGUAGES C CXX)
set(CMAKE_CXX_STANDARD 17)

option(ELIXIR_INSTALL "copy the VST3 and AU into the Mac's plug-in folders after building" OFF)

include(FetchContent)
FetchContent_Declare(JUCE GIT_REPOSITORY https://github.com/juce-framework/JUCE.git GIT_TAG 8.0.10 GIT_SHALLOW TRUE)
FetchContent_Declare(clap-juce-extensions GIT_REPOSITORY https://github.com/free-audio/clap-juce-extensions.git
    GIT_TAG 7adee3a1bd4684d4caa5601100e364abccff4b4d GIT_SUBMODULES_RECURSE TRUE)
FetchContent_MakeAvailable(JUCE clap-juce-extensions)

juce_add_binary_data(ElixirDesktopWeb SOURCES web/index.html)

set(formats VST3 Standalone)
if(APPLE)
    list(APPEND formats AU)
elseif(UNIX)
    list(APPEND formats LV2)
endif()

juce_add_plugin(ElixirDesktop
    VERSION 1.1.0
    COMPANY_NAME "Johnny"
    BUNDLE_ID "com.johnny.elixirdesktop"
    PLUGIN_MANUFACTURER_CODE Jhny
    PLUGIN_CODE Elxd
    PRODUCT_NAME "Elixir Desktop"
    IS_SYNTH TRUE
    NEEDS_MIDI_INPUT TRUE
    NEEDS_MIDI_OUTPUT FALSE
    IS_MIDI_EFFECT FALSE
    EDITOR_WANTS_KEYBOARD_FOCUS TRUE
    NEEDS_WEB_BROWSER TRUE
    NEEDS_WEBVIEW2 TRUE
    COPY_PLUGIN_AFTER_BUILD ${ELIXIR_INSTALL}
    VST3_COPY_DIR "$ENV{HOME}/Library/Audio/Plug-Ins/VST3/Johnny"
    VST3_CATEGORIES Instrument Drum
    AU_MAIN_TYPE kAudioUnitType_MusicDevice
    LV2URI "https://github.com/johnnyhawk02/elixir-desktop-vst"
    FORMATS ${formats})

clap_juce_extensions_plugin(TARGET ElixirDesktop
    CLAP_ID "com.johnny.elixirdesktop"
    CLAP_FEATURES instrument drum-machine stereo)

set(sources
    Source/Lxr.cpp
    Source/Player.cpp
    Source/Msg.cpp
    Source/Storage.cpp
    Source/PluginProcessor.cpp
    Source/WebHost.cpp
    Source/PluginEditor.cpp)
if(APPLE)
    list(APPEND sources Source/WebDialogs.mm)
endif()
target_sources(ElixirDesktop PRIVATE ${sources})

target_compile_definitions(ElixirDesktop PUBLIC
    JUCE_WEB_BROWSER=1
    JUCE_USE_CURL=0
    JUCE_VST3_CAN_REPLACE_VST2=0
    $<$<PLATFORM_ID:Windows>:JUCE_USE_WIN_WEBVIEW2_WITH_STATIC_LINKING=1>) # needs the WebView2 NuGet package, see .github/workflows

target_link_libraries(ElixirDesktop
    PRIVATE
        ElixirDesktopWeb
        juce::juce_audio_utils
        juce::juce_dsp
    PUBLIC
        juce::juce_recommended_config_flags
        juce::juce_recommended_lto_flags)

# the engine relies on JS-exact float rounding: no fused multiply-adds; and it compares floats exactly where the JS does
if(NOT MSVC)
    set_source_files_properties(Source/Lxr.cpp Source/Player.cpp PROPERTIES COMPILE_OPTIONS "-ffp-contract=off;-Wno-float-equal")
    set_source_files_properties(Source/Msg.cpp PROPERTIES COMPILE_OPTIONS "-Wno-float-equal")
else()
    set_source_files_properties(Source/Lxr.cpp Source/Player.cpp PROPERTIES COMPILE_OPTIONS "/fp:precise")
endif()

# ElixirDesktop_Compare: runs the C++ player on a message script, for tools/compare.mjs (JS vs C++, sample for sample)
juce_add_console_app(ElixirDesktop_Compare PRODUCT_NAME "ElixirDesktop_Compare")
set_target_properties(ElixirDesktop_Compare PROPERTIES EXCLUDE_FROM_ALL TRUE)
target_sources(ElixirDesktop_Compare PRIVATE tools/compare/Main.cpp Source/Lxr.cpp Source/Player.cpp Source/Msg.cpp)
target_include_directories(ElixirDesktop_Compare PRIVATE Source)
target_compile_definitions(ElixirDesktop_Compare PRIVATE JUCE_WEB_BROWSER=0 JUCE_USE_CURL=0)
target_link_libraries(ElixirDesktop_Compare PRIVATE juce::juce_core juce::juce_recommended_config_flags)
