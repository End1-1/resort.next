cmake_minimum_required(VERSION 3.21)

# CMake only. Do not add a qmake .pro for this service.
if(CMAKE_SOURCE_DIR STREQUAL CMAKE_CURRENT_SOURCE_DIR)
    project(hotel-api VERSION 0.5.0 LANGUAGES CXX)
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
    src/iniparse.cpp
    src/iniparse.h
    src/db.cpp
    src/db.h
    src/dberror.cpp
    src/dberror.h
    src/mysqlclient.cpp
    src/mysqlclient.h
    src/healthcheck.cpp
    src/healthcheck.h
    src/httpserver.cpp
    src/httpserver.h
    src/sessions.cpp
    src/sessions.h
    src/auth.cpp
    src/auth.h
    src/dictionaries.cpp
    src/dictionaries.h
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

target_compile_definitions(hotel-api PRIVATE HOTEL_API_VERSION="0.5.0")

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

find_package(Qt6 REQUIRED COMPONENTS Test)

add_executable(hotel-api-config-test
    src/iniparse.cpp
    src/iniparse.h
    src/config.cpp
    src/config.h
    src/dberror.cpp
    src/dberror.h
    tests/test_iniparse.cpp
    tests/test_config.cpp
)
target_include_directories(hotel-api-config-test PRIVATE src)
target_link_libraries(hotel-api-config-test PRIVATE Qt6::Core Qt6::Network Qt6::Test)
target_compile_definitions(hotel-api-config-test PRIVATE
    "HOTEL_API_INI_EXAMPLE=\"${CMAKE_CURRENT_SOURCE_DIR}/config/hotel-api.ini.example\""
)
if(MSVC)
    target_compile_options(hotel-api-config-test PRIVATE /W4 /utf-8)
else()
    target_compile_options(hotel-api-config-test PRIVATE -Wall -Wextra -Wpedantic)
endif()

add_executable(hotel-api-dberror-test
    src/dberror.cpp
    src/dberror.h
    src/mysqlclient.cpp
    src/mysqlclient.h
    tests/test_dberror.cpp
)
target_include_directories(hotel-api-dberror-test PRIVATE src)
target_link_libraries(hotel-api-dberror-test PRIVATE Qt6::Core Qt6::Sql Qt6::Test)
if(MSVC)
    target_compile_options(hotel-api-dberror-test PRIVATE /W4 /utf-8)
else()
    target_compile_options(hotel-api-dberror-test PRIVATE -Wall -Wextra -Wpedantic)
endif()

if(CMAKE_SOURCE_DIR STREQUAL CMAKE_CURRENT_SOURCE_DIR)
    enable_testing()
endif()
add_executable(hotel-api-auth-test
    src/auth.cpp
    src/auth.h
    src/db.cpp
    src/db.h
    src/dberror.cpp
    src/dberror.h
    src/mysqlclient.cpp
    src/mysqlclient.h
    tests/test_auth.cpp
)
target_include_directories(hotel-api-auth-test PRIVATE src)
target_link_libraries(hotel-api-auth-test PRIVATE Qt6::Core Qt6::Network Qt6::Sql Qt6::Test)
target_compile_definitions(hotel-api-auth-test PRIVATE
    "HOTEL_MIGRATION_0002=\"${CMAKE_CURRENT_SOURCE_DIR}/../dbdump/migrations/0002_nx_core.sql\""
)
if(MSVC)
    target_compile_options(hotel-api-auth-test PRIVATE /W4 /utf-8)
else()
    target_compile_options(hotel-api-auth-test PRIVATE -Wall -Wextra -Wpedantic)
endif()
add_dependencies(hotel-api-auth-test hotel-api)

add_test(NAME hotel-api-config-test COMMAND hotel-api-config-test)
add_test(NAME hotel-api-dberror-test COMMAND hotel-api-dberror-test)
add_executable(hotel-api-dictionary-test
    src/dictionaries.cpp
    src/dictionaries.h
    src/auth.cpp
    src/auth.h
    src/db.cpp
    src/db.h
    src/dberror.cpp
    src/dberror.h
    src/mysqlclient.cpp
    src/mysqlclient.h
    tests/test_dictionaries.cpp
)
target_include_directories(hotel-api-dictionary-test PRIVATE src)
target_link_libraries(hotel-api-dictionary-test PRIVATE Qt6::Core Qt6::Network Qt6::Sql Qt6::Test)
target_compile_definitions(hotel-api-dictionary-test PRIVATE
    "HOTEL_MIGRATION_0002=\"${CMAKE_CURRENT_SOURCE_DIR}/../dbdump/migrations/0002_nx_core.sql\""
    "HOTEL_MIGRATION_0003=\"${CMAKE_CURRENT_SOURCE_DIR}/../dbdump/migrations/0003_nx_label.sql\""
)
if(MSVC)
    target_compile_options(hotel-api-dictionary-test PRIVATE /W4 /utf-8)
else()
    target_compile_options(hotel-api-dictionary-test PRIVATE -Wall -Wextra -Wpedantic)
endif()
add_dependencies(hotel-api-dictionary-test hotel-api)

add_test(NAME hotel-api-auth-test COMMAND hotel-api-auth-test)
add_test(NAME hotel-api-dictionary-test COMMAND hotel-api-dictionary-test)

include(GNUInstallDirs)
install(TARGETS hotel-api RUNTIME DESTINATION ${CMAKE_INSTALL_BINDIR})
install(FILES config/hotel-api.ini.example
    DESTINATION ${CMAKE_INSTALL_BINDIR}
    RENAME hotel-api.ini.example)
