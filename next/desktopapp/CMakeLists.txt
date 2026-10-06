cmake_minimum_required(VERSION 3.21)

# CMake only. hotel-desktop is the windowed client. hotel-desktop-stub stays headless.
if(CMAKE_SOURCE_DIR STREQUAL CMAKE_CURRENT_SOURCE_DIR)
    project(hotel-desktop VERSION 0.1.0 LANGUAGES CXX)
    set(CMAKE_CXX_STANDARD 17)
    set(CMAKE_CXX_STANDARD_REQUIRED ON)
    set(CMAKE_CXX_EXTENSIONS OFF)
endif()

set(CMAKE_AUTOMOC ON)

find_package(Qt6 REQUIRED COMPONENTS Core Network Widgets WebSockets)

function(hotel_desktop_warnings target)
    if(MSVC)
        target_compile_options(${target} PRIVATE /W4 /utf-8)
    else()
        target_compile_options(${target} PRIVATE -Wall -Wextra -Wpedantic)
    endif()
endfunction()

add_library(hotel-desktop-api STATIC
    src/apiclient.cpp
    src/apiclient.h
    src/urlutil.cpp
    src/urlutil.h
)
target_include_directories(hotel-desktop-api PUBLIC ${CMAKE_CURRENT_SOURCE_DIR}/src)
target_link_libraries(hotel-desktop-api PUBLIC Qt6::Core Qt6::Network)
hotel_desktop_warnings(hotel-desktop-api)

add_library(hotel-desktop-ui STATIC
    src/appconfig.cpp
    src/appconfig.h
    src/appcontroller.cpp
    src/appcontroller.h
    src/connectiondialog.cpp
    src/connectiondialog.h
    src/healthmonitor.cpp
    src/healthmonitor.h
    src/loginwindow.cpp
    src/loginwindow.h
    src/mainwindow.cpp
    src/mainwindow.h
)
target_include_directories(hotel-desktop-ui PUBLIC ${CMAKE_CURRENT_SOURCE_DIR}/src)
target_link_libraries(hotel-desktop-ui PUBLIC hotel-desktop-api Qt6::Widgets Qt6::WebSockets)
hotel_desktop_warnings(hotel-desktop-ui)

# WIN32: GUI subsystem on Windows (WinMain via qt_add_executable). No extra console.
# Ignored on Linux, where the same binary still opens a window.
qt_add_executable(hotel-desktop WIN32
    src/main.cpp
)
target_link_libraries(hotel-desktop PRIVATE hotel-desktop-ui)
hotel_desktop_warnings(hotel-desktop)
set_target_properties(hotel-desktop PROPERTIES WIN32_EXECUTABLE TRUE)

add_executable(hotel-desktop-stub
    src/stubmain.cpp
)
target_link_libraries(hotel-desktop-stub PRIVATE hotel-desktop-api)
hotel_desktop_warnings(hotel-desktop-stub)

# Example only. The client reads hotel-desktop.ini beside the exe, never this name.
add_custom_command(TARGET hotel-desktop POST_BUILD
    COMMAND ${CMAKE_COMMAND} -E copy_if_different
        "${CMAKE_CURRENT_SOURCE_DIR}/hotel-desktop.ini.example"
        "$<TARGET_FILE_DIR:hotel-desktop>/hotel-desktop.ini.example"
    VERBATIM
)

include(GNUInstallDirs)
install(TARGETS hotel-desktop RUNTIME DESTINATION ${CMAKE_INSTALL_BINDIR})
install(FILES hotel-desktop.ini.example DESTINATION ${CMAKE_INSTALL_BINDIR})

if(BUILD_TESTING)
    find_package(Qt6 REQUIRED COMPONENTS Test)
    add_executable(hotel-desktop-login-smoke
        tests/login_smoke.cpp
    )
    target_link_libraries(hotel-desktop-login-smoke PRIVATE hotel-desktop-ui Qt6::Test)
    hotel_desktop_warnings(hotel-desktop-login-smoke)
    add_test(NAME hotel-desktop-login-smoke COMMAND hotel-desktop-login-smoke)
endif()
