cmake_minimum_required(VERSION 3.22)

# Statically link the MSVC runtime so the .exe runs on any Windows PC without the VC++ Redistributable.
cmake_policy(SET CMP0091 NEW)
set(CMAKE_MSVC_RUNTIME_LIBRARY "MultiThreaded$<$<CONFIG:Debug>:Debug>")

project(AntiHowlNative VERSION 1.0.0 LANGUAGES C CXX)

set(CMAKE_CXX_STANDARD 17)
set(CMAKE_CXX_STANDARD_REQUIRED ON)
set(CMAKE_CXX_EXTENSIONS OFF)

option(AH_BUILD_TESTS "Build the JUCE-free DSP / AI core tests" ON)
option(AH_BUILD_APP   "Build the JUCE application (downloads JUCE on first configure)" ON)
option(AH_ENABLE_ASIO "Enable ASIO on Windows (JUCE >= 8.0.11 bundles the ASIO headers)" ON)

# ---------------------------------------------------------------------------------------------
# 1) Core tests - plain C++17, no JUCE, no internet needed
# ---------------------------------------------------------------------------------------------
if(AH_BUILD_TESTS)
    enable_testing()
    add_executable(core_tests tests/core_tests.cpp)
    target_include_directories(core_tests PRIVATE core)
    target_compile_options(core_tests PRIVATE $<$<CXX_COMPILER_ID:MSVC>:/utf-8> $<$<NOT:$<CXX_COMPILER_ID:MSVC>>:-O2>)
    add_test(NAME core_tests COMMAND core_tests)
endif()

# ---------------------------------------------------------------------------------------------
# 2) The application
# ---------------------------------------------------------------------------------------------
if(AH_BUILD_APP)
    include(FetchContent)
    FetchContent_Declare(JUCE
        GIT_REPOSITORY https://github.com/juce-framework/JUCE.git
        GIT_TAG        8.0.11
        GIT_SHALLOW    ON)
    FetchContent_MakeAvailable(JUCE)

    set(AH_ICON "${CMAKE_CURRENT_SOURCE_DIR}/assets/icon.png")
    if(EXISTS "${AH_ICON}")
        juce_add_gui_app(AntiHowlNative
            PRODUCT_NAME "AntiHowl AI"
            COMPANY_NAME "AntiHowl"
            VERSION ${PROJECT_VERSION}
            ICON_BIG "${AH_ICON}")
    else()
        juce_add_gui_app(AntiHowlNative
            PRODUCT_NAME "AntiHowl AI"
            COMPANY_NAME "AntiHowl"
            VERSION ${PROJECT_VERSION})
    endif()

    target_sources(AntiHowlNative PRIVATE
        Source/Main.cpp
        Source/AudioEngine.cpp
        Source/MainComponent.cpp
        Source/SpectrumEq.cpp)

    target_include_directories(AntiHowlNative PRIVATE core Source)

    target_compile_definitions(AntiHowlNative PRIVATE
        JUCE_WEB_BROWSER=0
        JUCE_USE_CURL=0
        JUCE_STRICT_REFCOUNTEDPOINTER=1
        JUCE_WASAPI=1
        JUCE_DIRECTSOUND=0)

    if(WIN32 AND AH_ENABLE_ASIO)
        target_compile_definitions(AntiHowlNative PRIVATE JUCE_ASIO=1)
    endif()

    target_compile_options(AntiHowlNative PRIVATE $<$<CXX_COMPILER_ID:MSVC>:/utf-8>)

    target_link_libraries(AntiHowlNative PRIVATE
        juce::juce_audio_utils
        juce::juce_audio_devices
        juce::juce_audio_basics
        juce::juce_gui_basics
        juce::juce_data_structures
        juce::juce_events
        juce::juce_graphics
        juce::juce_core
        juce::juce_recommended_config_flags
        juce::juce_recommended_lto_flags)
endif()
