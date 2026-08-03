cmake_minimum_required(VERSION 3.16)

project(TeamSync VERSION 1.0.1 LANGUAGES CXX)

set(CMAKE_CXX_STANDARD 17)
set(CMAKE_CXX_STANDARD_REQUIRED ON)
set(CMAKE_CXX_EXTENSIONS OFF)

option(TEAMSYNC_BUILD_TESTS "Build TeamSync tests" ON)
option(TEAMSYNC_BUILD_GUI "Build the native Qt desktop interface" ON)

add_library(teamsync_core
    src/Utils.cpp
    src/Date.cpp
    src/User.cpp
    src/Team.cpp
    src/Project.cpp
    src/Task.cpp
    src/TaskAttachment.cpp
    src/Message.cpp
    src/SharedFile.cpp
    src/Notification.cpp
    src/ActivityLog.cpp
    src/FileManager.cpp
    src/Authentication.cpp
    src/TeamSyncService.cpp
    src/Dashboard.cpp
    src/ReportGenerator.cpp
    src/Input.cpp
    src/TeamSyncApplication.cpp
)
target_include_directories(teamsync_core PUBLIC include)

if(MSVC)
    target_compile_options(teamsync_core PRIVATE /W4 /permissive-)
else()
    target_compile_options(teamsync_core PRIVATE -Wall -Wextra -Wpedantic)
endif()

add_executable(teamsync src/main.cpp)
target_link_libraries(teamsync PRIVATE teamsync_core)

if(MINGW)
    # Keep the produced console executable runnable without copying GCC runtime DLLs.
    target_link_options(teamsync PRIVATE -static -static-libgcc -static-libstdc++)
endif()

if(WIN32)
    target_link_libraries(teamsync_core PRIVATE shell32)
endif()

if(TEAMSYNC_BUILD_GUI)
    find_package(Qt6 COMPONENTS Widgets Network QUIET)
    if(Qt6_FOUND)
        add_executable(teamsync_gui WIN32
            src/gui_main.cpp
            src/TeamSyncGui.cpp
            src/LanSyncManager.cpp
        )
        target_link_libraries(teamsync_gui PRIVATE teamsync_core Qt6::Widgets Qt6::Network)
        set_target_properties(teamsync_gui PROPERTIES
            RUNTIME_OUTPUT_DIRECTORY "${CMAKE_BINARY_DIR}/gui"
        )
        if(MINGW)
            target_link_options(teamsync_gui PRIVATE -static-libgcc -static-libstdc++)
        endif()
        if(WIN32)
            get_target_property(QT_QMAKE_EXECUTABLE Qt6::qmake IMPORTED_LOCATION)
            get_filename_component(QT_BIN_DIRECTORY "${QT_QMAKE_EXECUTABLE}" DIRECTORY)
            find_program(WINDEPLOYQT_EXECUTABLE windeployqt HINTS "${QT_BIN_DIRECTORY}")
            if(WINDEPLOYQT_EXECUTABLE)
                add_custom_command(TARGET teamsync_gui POST_BUILD
                    COMMAND "${WINDEPLOYQT_EXECUTABLE}" --release --no-translations
                            --compiler-runtime "$<TARGET_FILE:teamsync_gui>"
                    COMMENT "Deploying the Qt runtime beside TeamSync GUI"
                )
            endif()
        endif()
    else()
        message(STATUS "Qt 6 Widgets not found; the console application will still be built")
    endif()
endif()

if(TEAMSYNC_BUILD_TESTS)
    enable_testing()
    add_executable(teamsync_tests tests/teamsync_tests.cpp)
    target_link_libraries(teamsync_tests PRIVATE teamsync_core)
    if(MINGW)
        target_link_options(teamsync_tests PRIVATE -static -static-libgcc -static-libstdc++)
    endif()
    add_test(NAME TeamSyncTests COMMAND teamsync_tests)
endif()
