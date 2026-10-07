cmake_minimum_required(VERSION 3.21)

# CMake only. hotel-desktop is the windowed client. hotel-desktop-stub stays headless.
if(CMAKE_SOURCE_DIR STREQUAL CMAKE_CURRENT_SOURCE_DIR)
    project(hotel-desktop VERSION 0.1.0 LANGUAGES CXX)
    set(CMAKE_CXX_STANDARD 17)
    set(CMAKE_CXX_STANDARD_REQUIRED ON)
    set(CMAKE_CXX_EXTENSIONS OFF)
endif()

set(CMAKE_AUTOMOC ON)

find_package(Qt6 REQUIRED COMPONENTS Core Network Widgets WebSockets LinguistTools)

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
    src/dictionariespage.cpp
    src/dictionariespage.h
    src/rackpage.cpp
    src/rackpage.h
    src/reservationdialog.cpp
    src/reservationdialog.h
    src/reservationspage.cpp
    src/reservationspage.h
    src/workspacepage.cpp
    src/workspacepage.h
    src/healthmonitor.cpp
    src/healthmonitor.h
    src/uilanguage.cpp
    src/uilanguage.h
    src/loginwindow.cpp
    src/loginwindow.h
    src/mainwindow.cpp
    src/mainwindow.h
    resources/flags.qrc
)
set_target_properties(hotel-desktop-ui PROPERTIES AUTORCC ON)
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

# Qt 6.4 and 6.10 both accept TS_FILES / SOURCES / RESOURCE_PREFIX.
# lupdate is the hotel-desktop_lupdate target (not every build). lrelease
# embeds hotel-desktop_<lang>.qm under :/i18n/.
set(HOTEL_DESKTOP_TS_FILES
    ${CMAKE_CURRENT_SOURCE_DIR}/translations/hotel-desktop_en.ts
    ${CMAKE_CURRENT_SOURCE_DIR}/translations/hotel-desktop_ru.ts
    ${CMAKE_CURRENT_SOURCE_DIR}/translations/hotel-desktop_hy.ts
)
set(HOTEL_DESKTOP_I18N_SOURCES
    ${CMAKE_CURRENT_SOURCE_DIR}/src/apiclient.cpp
    ${CMAKE_CURRENT_SOURCE_DIR}/src/appconfig.cpp
    ${CMAKE_CURRENT_SOURCE_DIR}/src/appcontroller.cpp
    ${CMAKE_CURRENT_SOURCE_DIR}/src/connectiondialog.cpp
    ${CMAKE_CURRENT_SOURCE_DIR}/src/dictionariespage.cpp
    ${CMAKE_CURRENT_SOURCE_DIR}/src/rackpage.cpp
    ${CMAKE_CURRENT_SOURCE_DIR}/src/reservationdialog.cpp
    ${CMAKE_CURRENT_SOURCE_DIR}/src/reservationspage.cpp
    ${CMAKE_CURRENT_SOURCE_DIR}/src/workspacepage.cpp
    ${CMAKE_CURRENT_SOURCE_DIR}/src/healthmonitor.cpp
    ${CMAKE_CURRENT_SOURCE_DIR}/src/uilanguage.cpp
    ${CMAKE_CURRENT_SOURCE_DIR}/src/loginwindow.cpp
    ${CMAKE_CURRENT_SOURCE_DIR}/src/main.cpp
    ${CMAKE_CURRENT_SOURCE_DIR}/src/mainwindow.cpp
    ${CMAKE_CURRENT_SOURCE_DIR}/src/urlutil.cpp
)
qt_add_translations(hotel-desktop
    TS_FILES ${HOTEL_DESKTOP_TS_FILES}
    SOURCES ${HOTEL_DESKTOP_I18N_SOURCES}
    RESOURCE_PREFIX "/i18n"
)
set(hotel_desktop_qm_files
    ${CMAKE_CURRENT_BINARY_DIR}/hotel-desktop_en.qm
    ${CMAKE_CURRENT_BINARY_DIR}/hotel-desktop_ru.qm
    ${CMAKE_CURRENT_BINARY_DIR}/hotel-desktop_hy.qm
)

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
    qt_add_resources(hotel-desktop-login-smoke hotel_desktop_i18n_test
        PREFIX "/i18n"
        BASE "${CMAKE_CURRENT_BINARY_DIR}"
        FILES ${hotel_desktop_qm_files}
    )
    add_dependencies(hotel-desktop-login-smoke hotel-desktop_lrelease)
    add_test(NAME hotel-desktop-login-smoke COMMAND hotel-desktop-login-smoke)
endif()
