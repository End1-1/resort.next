cmake_minimum_required(VERSION 3.21)

# CMake only. The reception UI can become Qt Widgets later; this stub is headless
# so it builds without a display and without Qt Sql.
if(CMAKE_SOURCE_DIR STREQUAL CMAKE_CURRENT_SOURCE_DIR)
    project(hotel-desktop-stub VERSION 0.1.0 LANGUAGES CXX)
    set(CMAKE_CXX_STANDARD 17)
    set(CMAKE_CXX_STANDARD_REQUIRED ON)
    set(CMAKE_CXX_EXTENSIONS OFF)
endif()

set(CMAKE_AUTOMOC ON)

find_package(Qt6 REQUIRED COMPONENTS Core Network)

add_executable(hotel-desktop-stub
    src/main.cpp
    src/apiclient.cpp
    src/apiclient.h
)

target_link_libraries(hotel-desktop-stub PRIVATE
    Qt6::Core
    Qt6::Network
)

if(MSVC)
    target_compile_options(hotel-desktop-stub PRIVATE /W4 /utf-8)
else()
    target_compile_options(hotel-desktop-stub PRIVATE -Wall -Wextra -Wpedantic)
endif()
