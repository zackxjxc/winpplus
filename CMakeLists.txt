cmake_minimum_required(VERSION 3.25)

file(READ "${CMAKE_CURRENT_SOURCE_DIR}/VERSION" WINPPLUS_VERSION)
string(STRIP "${WINPPLUS_VERSION}" WINPPLUS_VERSION)
if(NOT WINPPLUS_VERSION MATCHES "^(0|[1-9][0-9]*)\\.(0|[1-9][0-9]*)\\.(0|[1-9][0-9]*)$")
    message(FATAL_ERROR "VERSION must contain a stable MAJOR.MINOR.PATCH version.")
endif()
string(REPLACE "." ";" WINPPLUS_VERSION_COMPONENTS "${WINPPLUS_VERSION}")
foreach(component IN LISTS WINPPLUS_VERSION_COMPONENTS)
    if(component GREATER 65535)
        message(FATAL_ERROR "Version components must fit Windows version resources (0-65535).")
    endif()
endforeach()

set(CMAKE_SYSTEM_VERSION "10.0.26100.0" CACHE STRING "Windows SDK version")
project(WinPPlus VERSION "${WINPPLUS_VERSION}" LANGUAGES C CXX RC)
if(NOT WIN32 OR NOT MSVC OR NOT CMAKE_CXX_COMPILER_ARCHITECTURE_ID STREQUAL "x64")
    message(FATAL_ERROR "WinPPlus requires Windows x64 and the MSVC toolchain.")
endif()

set(CMAKE_CXX_STANDARD 20)
set(CMAKE_CXX_STANDARD_REQUIRED ON)
set(CMAKE_CXX_EXTENSIONS OFF)
set(CMAKE_MSVC_RUNTIME_LIBRARY "MultiThreaded$<$<CONFIG:Debug>:Debug>")
set(CMAKE_RUNTIME_OUTPUT_DIRECTORY "${CMAKE_CURRENT_BINARY_DIR}/bin/$<CONFIG>")
set(CMAKE_ARCHIVE_OUTPUT_DIRECTORY "${CMAKE_CURRENT_BINARY_DIR}/lib/$<CONFIG>")
set_property(DIRECTORY APPEND PROPERTY CMAKE_CONFIGURE_DEPENDS "${CMAKE_CURRENT_SOURCE_DIR}/VERSION")
configure_file(cmake/app_version.h.in generated/winpplus_version.generated.h @ONLY)
configure_file(cmake/resources.h.in generated/winpplus_resources.generated.h @ONLY)
configure_file(cmake/app.manifest.in generated/WinPPlus.manifest @ONLY)

add_library(winpplus_build_options INTERFACE)
target_compile_definitions(winpplus_build_options INTERFACE
    UNICODE _UNICODE WIN32_LEAN_AND_MEAN NOMINMAX _WIN32_WINNT=0x0A00)
target_compile_options(winpplus_build_options INTERFACE
    "$<$<COMPILE_LANGUAGE:C,CXX>:/W4;/WX;/sdl;/utf-8>"
    "$<$<COMPILE_LANGUAGE:CXX>:/permissive->")

add_library(MinHook STATIC
    vendor/minhook/src/buffer.c
    vendor/minhook/src/hook.c
    vendor/minhook/src/trampoline.c
    vendor/minhook/src/hde/hde64.c)
target_include_directories(MinHook PUBLIC vendor/minhook/include PRIVATE vendor/minhook/src)
target_link_libraries(MinHook PRIVATE winpplus_build_options)
target_compile_options(MinHook PRIVATE /wd4201 /wd4310 /wd4244)

add_library(WinPPlusHook SHARED src/shell_hook.cpp)
target_include_directories(WinPPlusHook PRIVATE src)
target_compile_definitions(WinPPlusHook PRIVATE WIN32 _WINDOWS _USRDLL)
target_link_libraries(WinPPlusHook PRIVATE winpplus_build_options MinHook user32 shcore)
target_link_options(WinPPlusHook PRIVATE /PDBALTPATH:WinPPlusHook.pdb)

# A file-level dependency updates the embedded resource whenever the DLL changes.
set(hook_resource "${CMAKE_CURRENT_BINARY_DIR}/generated/$<CONFIG>/hook_resource.rc")
file(GENERATE OUTPUT "${hook_resource}.in" CONTENT "201 RCDATA \"$<TARGET_FILE:WinPPlusHook>\"\n")
add_custom_command(OUTPUT "${hook_resource}"
    COMMAND "${CMAKE_COMMAND}" -E copy "${hook_resource}.in" "${hook_resource}"
    DEPENDS WinPPlusHook "${hook_resource}.in"
    VERBATIM)

add_executable(WinPPlus WIN32
    src/main.cpp
    src/application.cpp
    src/tray_window.cpp
    src/settings_window.cpp
    src/projection_menu.cpp
    src/app_storage.cpp
    src/startup_registration.cpp
    src/display_state.cpp
    src/display_switch.cpp
    src/modern_ui.cpp
    src/popup_surface.cpp
    src/native_hook_controller.cpp
    src/tray_resources.rc
    "${CMAKE_CURRENT_BINARY_DIR}/generated/WinPPlus.manifest"
    tests/app_self_test.cpp
    tests/startup_registration_test.cpp
    "${hook_resource}")
target_include_directories(WinPPlus PRIVATE src "${CMAKE_CURRENT_BINARY_DIR}/generated")
target_link_libraries(WinPPlus PRIVATE winpplus_build_options
    user32 gdi32 gdiplus shell32 dwmapi shcore ole32 oleaut32 advapi32)
target_link_options(WinPPlus PRIVATE /ENTRY:wWinMainCRTStartup /PDBALTPATH:WinPPlus.pdb)
set_property(DIRECTORY PROPERTY VS_STARTUP_PROJECT WinPPlus)

include(CTest)
if(BUILD_TESTING)
    find_program(WINPPLUS_POWERSHELL NAMES pwsh powershell REQUIRED)
    add_test(NAME WinPPlus.self-test COMMAND "${WINPPLUS_POWERSHELL}" -NoProfile -ExecutionPolicy Bypass
        -File "${CMAKE_CURRENT_SOURCE_DIR}/scripts/test.ps1" -NoBuild
        -Configuration "$<CONFIG>" -ExecutablePath "$<TARGET_FILE:WinPPlus>")
    add_test(NAME WinPPlus.release-rules COMMAND "${WINPPLUS_POWERSHELL}" -NoProfile -ExecutionPolicy Bypass
        -File "${CMAKE_CURRENT_SOURCE_DIR}/scripts/test-release.ps1")
    set_tests_properties(WinPPlus.self-test PROPERTIES TIMEOUT 45 RUN_SERIAL TRUE)
    set_tests_properties(WinPPlus.release-rules PROPERTIES TIMEOUT 30)
endif()

install(TARGETS WinPPlus RUNTIME DESTINATION .)
install(FILES README.md README.zh-CN.md LICENSE THIRD_PARTY_NOTICES.md VERSION DESTINATION .)
install(DIRECTORY docs/ DESTINATION docs)
