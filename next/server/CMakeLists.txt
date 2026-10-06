cmake_minimum_required(VERSION 3.21)

# CMake only. Do not add a qmake .pro for this service.
if(CMAKE_SOURCE_DIR STREQUAL CMAKE_CURRENT_SOURCE_DIR)
    project(hotel-api VERSION 0.3.0 LANGUAGES CXX)
    set(CMAKE_CXX_STANDARD 17)
    set(CMAKE_CXX_STANDARD_REQUIRED ON)
    set(CMAKE_CXX_EXTENSIONS OFF)
endif()

set(CMAKE_AUTOMOC ON)

find_package(Qt6 REQUIRED COMPONENTS Core Network Sql HttpServer WebSockets)

set(HOTEL_API_SOURCES
    src/main.cpp
    src/config.cpp
    src/config.h
    src/db.cpp
    src/db.h
    src/healthcheck.cpp
    src/healthcheck.h
    src/httpserver.cpp
    src/httpserver.h
    src/sessions.cpp
    src/sessions.h
    src/realtimehub.cpp
    src/realtimehub.h
    src/runner.cpp
    src/runner.h
    src/version.h
    src/platform.h
)

if(WIN32)
    list(APPEND HOTEL_API_SOURCES src/platform_windows.cpp)
else()
    list(APPEND HOTEL_API_SOURCES src/platform_unix.cpp)
endif()

add_executable(hotel-api ${HOTEL_API_SOURCES})

target_compile_definitions(hotel-api PRIVATE HOTEL_API_VERSION="0.3.0")

target_link_libraries(hotel-api PRIVATE
    Qt6::Core
    Qt6::Network
    Qt6::Sql
    Qt6::HttpServer
    Qt6::WebSockets
)

if(WIN32)
    target_link_libraries(hotel-api PRIVATE advapi32)
endif()

if(MSVC)
    target_compile_options(hotel-api PRIVATE /W4 /utf-8)
else()
    target_compile_options(hotel-api PRIVATE -Wall -Wextra -Wpedantic)
endif()

# Example only. The process looks for hotel-api.ini, never this filename.
add_custom_command(TARGET hotel-api POST_BUILD
    COMMAND ${CMAKE_COMMAND} -E copy_if_different
        "${CMAKE_CURRENT_SOURCE_DIR}/config/hotel-api.ini.example"
        "$<TARGET_FILE_DIR:hotel-api>/hotel-api.ini.example"
    VERBATIM
)

include(GNUInstallDirs)
install(TARGETS hotel-api RUNTIME DESTINATION ${CMAKE_INSTALL_BINDIR})
install(FILES config/hotel-api.ini.example
    DESTINATION ${CMAKE_INSTALL_BINDIR}
    RENAME hotel-api.ini.example)
