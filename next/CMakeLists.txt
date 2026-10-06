cmake_minimum_required(VERSION 3.21)

# New migration code lives under next/ and is CMake-only (not qmake, not Go).
project(hotel-next LANGUAGES CXX)

set(CMAKE_CXX_STANDARD 17)
set(CMAKE_CXX_STANDARD_REQUIRED ON)
set(CMAKE_CXX_EXTENSIONS OFF)

# include(CTest) defines BUILD_TESTING (default ON) and calls enable_testing().
# That covers the enable_testing() call that landed on main, and it registers
# both hotel-api-config-test and hotel-desktop-login-smoke.
include(CTest)

add_subdirectory(server)
add_subdirectory(desktopapp)
