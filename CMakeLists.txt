cmake_minimum_required(VERSION 3.20)

project(
    Apadana
    VERSION 0.3.0
    DESCRIPTION "A distro-agnostic Unix/Linux system manager"
    LANGUAGES CXX
)

set(CMAKE_CXX_STANDARD 17)
set(CMAKE_CXX_STANDARD_REQUIRED ON)

option(APADANA_BUILD_TESTS "Build Apadana tests" ON)

set(CURSES_NEED_WIDE TRUE)
find_package(Curses REQUIRED)

add_library(apadana_core
    src/core/command.cpp
    src/core/package_manager.cpp
    src/core/platform.cpp
    src/core/process_manager.cpp
    src/core/service_manager.cpp
    src/core/system_info.cpp
    src/core/user_manager.cpp
)

target_include_directories(apadana_core PUBLIC include)
set_target_properties(apadana_core PROPERTIES CXX_EXTENSIONS OFF)

if(CMAKE_CXX_COMPILER_ID MATCHES "GNU|Clang")
    target_compile_options(apadana_core PRIVATE -Wall -Wextra -Wpedantic -Wconversion)
elseif(MSVC)
    target_compile_options(apadana_core PRIVATE /W4 /permissive-)
endif()

add_executable(apadana
    src/main.cpp
    src/tui/application.cpp
)
target_include_directories(apadana PRIVATE ${CURSES_INCLUDE_DIRS})
target_link_libraries(apadana PRIVATE apadana_core ${CURSES_LIBRARIES})

if(CMAKE_CXX_COMPILER_ID MATCHES "GNU|Clang")
    target_compile_options(apadana PRIVATE -Wall -Wextra -Wpedantic -Wconversion)
elseif(MSVC)
    target_compile_options(apadana PRIVATE /W4 /permissive-)
endif()

include(GNUInstallDirs)
install(TARGETS apadana RUNTIME DESTINATION "${CMAKE_INSTALL_BINDIR}")

if(APADANA_BUILD_TESTS)
    enable_testing()
    add_executable(apadana_tests tests/core_tests.cpp)
    target_link_libraries(apadana_tests PRIVATE apadana_core)
    add_test(NAME apadana_core_tests COMMAND apadana_tests)
endif()
