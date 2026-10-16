cmake_minimum_required(VERSION 3.22)
# The version is written once, in include/aion2/version.hpp, which app.rc reads too.
file(STRINGS "${CMAKE_CURRENT_SOURCE_DIR}/include/aion2/version.hpp" AION2_VERSION_DEFINES
     REGEX "^#define AION2_VERSION_(MAJOR|MINOR|PATCH) [0-9]+$")
foreach(part MAJOR MINOR PATCH)
    string(REGEX MATCH "AION2_VERSION_${part} ([0-9]+)" _match "${AION2_VERSION_DEFINES}")
    if(NOT _match)
        message(FATAL_ERROR "AION2_VERSION_${part} not found in include/aion2/version.hpp")
    endif()
    set(AION2_VERSION_${part} "${CMAKE_MATCH_1}")
endforeach()
project(aion2-combat-meter
        VERSION ${AION2_VERSION_MAJOR}.${AION2_VERSION_MINOR}.${AION2_VERSION_PATCH}
        LANGUAGES CXX)

set(CMAKE_CXX_STANDARD 20)
set(CMAKE_CXX_STANDARD_REQUIRED ON)
set(CMAKE_CXX_EXTENSIONS OFF)

# Runtime executables together in bin/.
set(CMAKE_RUNTIME_OUTPUT_DIRECTORY "${CMAKE_BINARY_DIR}/bin")
set(CMAKE_ARCHIVE_OUTPUT_DIRECTORY "${CMAKE_BINARY_DIR}/lib")
foreach(OUTPUTCONFIG ${CMAKE_CONFIGURATION_TYPES})
    string(TOUPPER ${OUTPUTCONFIG} OUTPUTCONFIG_UPPER)
    set(CMAKE_RUNTIME_OUTPUT_DIRECTORY_${OUTPUTCONFIG_UPPER} "${CMAKE_BINARY_DIR}/bin/${OUTPUTCONFIG}")
    set(CMAKE_ARCHIVE_OUTPUT_DIRECTORY_${OUTPUTCONFIG_UPPER} "${CMAKE_BINARY_DIR}/lib/${OUTPUTCONFIG}")
endforeach()

option(BUILD_TESTS "Build the unit test runner" ON)

if(MSVC)
    add_compile_options(/permissive- /MP /utf-8 /wd4324)
    add_compile_definitions(NOMINMAX WIN32_LEAN_AND_MEAN _CRT_SECURE_NO_WARNINGS UNICODE _UNICODE)
    # Static C/C++ runtime: no Visual C++ Redistributable to install.
    set(CMAKE_MSVC_RUNTIME_LIBRARY "MultiThreaded$<$<CONFIG:Debug>:Debug>")
endif()

# Warnings as errors for every target of ours. Linked PRIVATE, so it never reaches
# the vendored ImGui.
add_library(aion2_warnings INTERFACE)
if(MSVC)
    target_compile_options(aion2_warnings INTERFACE /W4 /WX)
else()
    target_compile_options(aion2_warnings INTERFACE -Wall -Wextra -Wpedantic -Werror)
endif()

if(WIN32)
    set(AION2_WINDOWS ON)
else()
    set(AION2_WINDOWS OFF)
endif()

find_package(Threads REQUIRED)

# -----------------------------------------------------------------------------
# Layers (AGENTS.md): common <- capture, protocol <- meter <- pipeline <- app.
# Each publishes only its own include root, so an #include across the layering
# does not compile.
# -----------------------------------------------------------------------------
file(GLOB_RECURSE AION2_COMMON_SOURCES CONFIGURE_DEPENDS src/common/src/*.cpp)
add_library(aion2_common STATIC ${AION2_COMMON_SOURCES})
target_include_directories(aion2_common PUBLIC include src/common/include)
target_link_libraries(aion2_common PUBLIC Threads::Threads PRIVATE aion2_warnings)
if(AION2_WINDOWS)
    target_link_libraries(aion2_common PUBLIC shell32)
endif()

file(GLOB_RECURSE AION2_CAPTURE_SOURCES CONFIGURE_DEPENDS src/capture/src/*.cpp)
add_library(aion2_capture STATIC ${AION2_CAPTURE_SOURCES})
target_include_directories(aion2_capture PUBLIC src/capture/include)
target_link_libraries(aion2_capture PUBLIC aion2_common PRIVATE aion2_warnings)
if(AION2_WINDOWS)
    # The TCP table and the process snapshot. Npcap's wpcap.dll is loaded at run
    # time, never linked.
    target_link_libraries(aion2_capture PUBLIC iphlpapi ws2_32)
endif()

file(GLOB AION2_PROTOCOL_SOURCES CONFIGURE_DEPENDS src/protocol/src/*.cpp)
add_library(aion2_protocol STATIC ${AION2_PROTOCOL_SOURCES})
target_include_directories(aion2_protocol PUBLIC src/protocol/include)
target_link_libraries(aion2_protocol PUBLIC aion2_common PRIVATE aion2_warnings)

file(GLOB AION2_METER_SOURCES CONFIGURE_DEPENDS src/meter/src/*.cpp)
add_library(aion2_meter STATIC ${AION2_METER_SOURCES})
target_include_directories(aion2_meter PUBLIC src/meter/include)
target_link_libraries(aion2_meter PUBLIC aion2_protocol PRIVATE aion2_warnings)

file(GLOB AION2_PIPELINE_SOURCES CONFIGURE_DEPENDS src/pipeline/src/*.cpp)
add_library(aion2_pipeline STATIC ${AION2_PIPELINE_SOURCES})
target_include_directories(aion2_pipeline PUBLIC src/pipeline/include)
target_link_libraries(aion2_pipeline PUBLIC aion2_capture aion2_meter PRIVATE aion2_warnings)

# Dear ImGui with its Win32 and DX11 backends: Windows only. Its headers are SYSTEM
# includes, so our warnings-as-errors leaves them alone.
if(AION2_WINDOWS)
    add_library(imgui STATIC
        src/third_party/imgui/imgui.cpp
        src/third_party/imgui/imgui_draw.cpp
        src/third_party/imgui/imgui_tables.cpp
        src/third_party/imgui/imgui_widgets.cpp
        src/third_party/imgui/backends/imgui_impl_win32.cpp
        src/third_party/imgui/backends/imgui_impl_dx11.cpp
    )
    target_include_directories(imgui SYSTEM PUBLIC src/third_party/imgui)
    target_compile_definitions(imgui PUBLIC IMGUI_IMPL_WIN32_DISABLE_GAMEPAD)
    if(MSVC)
        target_compile_options(imgui PRIVATE /W3 /wd4244 /wd4267)
    endif()
endif()

# The app less its wWinMain and window: what the tests link.
file(GLOB AION2_APP_SOURCES CONFIGURE_DEPENDS src/app/src/*.cpp)
add_library(aion2_app_core STATIC ${AION2_APP_SOURCES})
target_include_directories(aion2_app_core PUBLIC src/app/include)
target_link_libraries(aion2_app_core PUBLIC aion2_pipeline PRIVATE aion2_warnings)
if(AION2_WINDOWS)
    target_link_libraries(aion2_app_core PUBLIC imgui)
endif()

# aion2meter-replay: every platform.
file(GLOB AION2_REPLAY_SOURCES CONFIGURE_DEPENDS tools/replay/*.cpp)
add_executable(aion2meter-replay ${AION2_REPLAY_SOURCES})
target_link_libraries(aion2meter-replay PRIVATE aion2_pipeline aion2_warnings)

if(BUILD_TESTS)
    enable_testing()
    file(GLOB TEST_SOURCES CONFIGURE_DEPENDS tests/*.cpp)
    add_executable(aion2_test_runner ${TEST_SOURCES})
    target_include_directories(aion2_test_runner PRIVATE tests)
    target_link_libraries(aion2_test_runner PRIVATE aion2_app_core aion2_warnings)
    add_test(NAME AllTests COMMAND aion2_test_runner)

    add_executable(aion2meter-synth tools/synth/main.cpp)
    target_include_directories(aion2meter-synth PRIVATE tests)
    target_link_libraries(aion2meter-synth PRIVATE aion2_pipeline aion2_warnings)
endif()

if(AION2_WINDOWS)
    add_executable(aion2-combat-meter WIN32
        src/app/src/win32/main.cpp
        src/app/src/win32/overlay_window.cpp
        src/app/resources/app.rc
    )
    target_link_libraries(aion2-combat-meter PRIVATE aion2_app_core aion2_warnings d3d11 dxgi dwmapi)
    if(MSVC)
        # app.rc embeds app.manifest; the linker must not add a second one.
        target_link_options(aion2-combat-meter PRIVATE "/MANIFEST:NO")
    endif()

    install(TARGETS aion2-combat-meter aion2meter-replay RUNTIME DESTINATION .)
    install(FILES LICENSE README.md DESTINATION .)
endif()

# -----------------------------------------------------------------------------
# Packaging (CPack): the release zip, binaries at its root.
# -----------------------------------------------------------------------------
set(CPACK_PACKAGE_NAME "aion2-combat-meter")
set(CPACK_PACKAGE_VENDOR "aion2-combat-meter contributors")
set(CPACK_PACKAGE_DESCRIPTION_SUMMARY "AION 2 Combat Meter - passive party DPS meter with an overlay")
set(CPACK_PACKAGE_VERSION_MAJOR ${PROJECT_VERSION_MAJOR})
set(CPACK_PACKAGE_VERSION_MINOR ${PROJECT_VERSION_MINOR})
set(CPACK_PACKAGE_VERSION_PATCH ${PROJECT_VERSION_PATCH})
set(CPACK_GENERATOR "ZIP")
set(CPACK_PACKAGE_FILE_NAME "aion2-combat-meter-windows-x64")
set(CPACK_INCLUDE_TOPLEVEL_DIRECTORY OFF)
include(CPack)
