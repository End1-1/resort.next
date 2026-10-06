cmake_minimum_required(VERSION 3.21)

# New migration code lives under next/ and is CMake-only (not qmake, not Go).
project(hotel-next LANGUAGES CXX)

set(CMAKE_CXX_STANDARD 17)
set(CMAKE_CXX_STANDARD_REQUIRED ON)
set(CMAKE_CXX_EXTENSIONS OFF)

enable_testing()
add_subdirectory(server)
add_subdirectory(desktopapp)
