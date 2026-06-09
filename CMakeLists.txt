cmake_minimum_required(VERSION 3.16)
project(PulsePadGTK VERSION 1.0 LANGUAGES CXX)

set(CMAKE_CXX_STANDARD 17)
set(CMAKE_CXX_STANDARD_REQUIRED ON)
set(CMAKE_CXX_EXTENSIONS OFF)

option(PULSEPAD_ENABLE_WARNINGS "Enable compiler warnings" ON)
option(PULSEPAD_WARNINGS_AS_ERRORS "Treat warnings as errors" OFF)
option(PULSEPAD_BUILD_APP "Build GTK application" ON)

include(CTest)

add_library(pulsepad_warnings INTERFACE)
if(PULSEPAD_ENABLE_WARNINGS)
    target_compile_options(pulsepad_warnings INTERFACE
        $<$<CXX_COMPILER_ID:MSVC>:/W4>
        $<$<NOT:$<CXX_COMPILER_ID:MSVC>>:-Wall -Wextra -Wpedantic -Wconversion -Wsign-conversion>
    )
endif()
if(PULSEPAD_WARNINGS_AS_ERRORS)
    target_compile_options(pulsepad_warnings INTERFACE
        $<$<CXX_COMPILER_ID:MSVC>:/WX>
        $<$<NOT:$<CXX_COMPILER_ID:MSVC>>:-Werror>
    )
endif()

message(STATUS "PulsePad warnings: ${PULSEPAD_ENABLE_WARNINGS}")
message(STATUS "Warnings as errors: ${PULSEPAD_WARNINGS_AS_ERRORS}")
message(STATUS "Build GTK application: ${PULSEPAD_BUILD_APP}")

add_library(pulsepad_logic
    src/BoardConfig.cpp
    src/BoardPackage.cpp
    src/BoardRepository.cpp
    src/MidiController.cpp
    src/PadGridPresenter.cpp
    src/Subprocess.cpp
    src/TaskRunner.cpp
    src/UserMessages.cpp
    src/Waveform.cpp
)
target_include_directories(pulsepad_logic PUBLIC ${CMAKE_CURRENT_SOURCE_DIR}/src)
target_link_libraries(pulsepad_logic PRIVATE pulsepad_warnings)

if(BUILD_TESTING)
    add_executable(pulsepad_logic_tests
        tests/test_board_config.cpp
    )
    add_executable(pulsepad_package_tests
        tests/test_board_package.cpp
    )
    add_executable(pulsepad_subprocess_tests
        tests/test_subprocess.cpp
    )
    add_executable(pulsepad_task_runner_tests
        tests/test_task_runner.cpp
    )
    add_executable(pulsepad_midi_tests
        tests/test_midi_controller.cpp
    )
    add_executable(pulsepad_user_message_tests
        tests/test_user_messages.cpp
    )
    target_link_libraries(pulsepad_logic_tests PRIVATE pulsepad_logic pulsepad_warnings)
    target_link_libraries(pulsepad_package_tests PRIVATE pulsepad_logic pulsepad_warnings)
    target_link_libraries(pulsepad_subprocess_tests PRIVATE pulsepad_logic pulsepad_warnings)
    target_link_libraries(pulsepad_task_runner_tests PRIVATE pulsepad_logic pulsepad_warnings)
    target_link_libraries(pulsepad_midi_tests PRIVATE pulsepad_logic pulsepad_warnings)
    target_link_libraries(pulsepad_user_message_tests PRIVATE pulsepad_logic pulsepad_warnings)
    add_test(NAME pulsepad_logic_tests COMMAND pulsepad_logic_tests)
    add_test(NAME pulsepad_package_tests COMMAND pulsepad_package_tests)
    add_test(NAME pulsepad_subprocess_tests COMMAND pulsepad_subprocess_tests)
    add_test(NAME pulsepad_task_runner_tests COMMAND pulsepad_task_runner_tests)
    add_test(NAME pulsepad_midi_tests COMMAND pulsepad_midi_tests)
    add_test(NAME pulsepad_user_message_tests COMMAND pulsepad_user_message_tests)
    set_tests_properties(pulsepad_subprocess_tests PROPERTIES TIMEOUT 10)
endif()

if(PULSEPAD_BUILD_APP)
    find_package(PkgConfig QUIET)
    if(PkgConfig_FOUND)
        pkg_check_modules(GTKMM QUIET gtkmm-4.0)
        pkg_check_modules(GSTREAMER QUIET gstreamer-1.0)
        pkg_check_modules(LIBZIP QUIET libzip)
        pkg_check_modules(RTMIDI QUIET rtmidi)
    endif()
    find_package(nlohmann_json 3.2.0 QUIET)

    set(PULSEPAD_MISSING_APP_DEPENDENCIES)
    if(NOT PkgConfig_FOUND)
        list(APPEND PULSEPAD_MISSING_APP_DEPENDENCIES "pkg-config")
    endif()
    if(NOT GTKMM_FOUND)
        list(APPEND PULSEPAD_MISSING_APP_DEPENDENCIES "gtkmm-4.0")
    endif()
    if(NOT GSTREAMER_FOUND)
        list(APPEND PULSEPAD_MISSING_APP_DEPENDENCIES "gstreamer-1.0")
    endif()
    if(NOT LIBZIP_FOUND)
        list(APPEND PULSEPAD_MISSING_APP_DEPENDENCIES "libzip")
    endif()
    if(NOT nlohmann_json_FOUND)
        list(APPEND PULSEPAD_MISSING_APP_DEPENDENCIES "nlohmann_json >= 3.2.0")
    endif()

    if(PULSEPAD_MISSING_APP_DEPENDENCIES)
        message(STATUS "GTK dependencies found: NO")
        list(JOIN PULSEPAD_MISSING_APP_DEPENDENCIES ", " PULSEPAD_MISSING_APP_DEPENDENCIES_TEXT)
        message(FATAL_ERROR
            "PULSEPAD_BUILD_APP is ON, but required application dependencies are missing: "
            "${PULSEPAD_MISSING_APP_DEPENDENCIES_TEXT}. "
            "Install the missing GTK/GStreamer application packages or configure with "
            "-DPULSEPAD_BUILD_APP=OFF for a logic-only build."
        )
    endif()

    message(STATUS "GTK dependencies found: YES")

    add_executable(PulsePadGTK
        src/main.cpp
        src/PulsePadWindow.cpp
        src/UiDialogs.cpp
        src/BoardRepository.cpp
        src/BoardPackage.cpp
        src/BoardPackageZip.cpp
        src/AudioEngine.cpp
        src/Waveform.cpp
        src/MidiController.cpp
        src/PadGridPresenter.cpp
        src/BoardConfig.cpp
        src/Subprocess.cpp
        src/TaskRunner.cpp
        src/UserMessages.cpp
    )

    target_include_directories(PulsePadGTK PRIVATE
        ${CMAKE_CURRENT_SOURCE_DIR}/src
    )

    target_include_directories(PulsePadGTK SYSTEM PRIVATE
        ${GTKMM_INCLUDE_DIRS}
        ${GSTREAMER_INCLUDE_DIRS}
        ${LIBZIP_INCLUDE_DIRS}
        ${RTMIDI_INCLUDE_DIRS}
    )

    target_compile_options(PulsePadGTK PRIVATE
        ${GTKMM_CFLAGS_OTHER}
        ${GSTREAMER_CFLAGS_OTHER}
        ${LIBZIP_CFLAGS_OTHER}
        ${RTMIDI_CFLAGS_OTHER}
    )

    target_link_libraries(PulsePadGTK PRIVATE
        pulsepad_warnings
        ${GTKMM_LIBRARIES}
        ${GSTREAMER_LIBRARIES}
        ${LIBZIP_LIBRARIES}
        ${RTMIDI_LIBRARIES}
        nlohmann_json::nlohmann_json
    )

    install(TARGETS PulsePadGTK RUNTIME DESTINATION bin)

    if(RTMIDI_FOUND)
        target_compile_definitions(PulsePadGTK PRIVATE HAVE_RTMIDI=1)
    endif()
else()
    message(STATUS "GTK dependencies found: not checked (PULSEPAD_BUILD_APP=OFF)")
endif()
