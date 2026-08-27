cmake_minimum_required(VERSION 3.16)
project(media_core CXX)

set(CMAKE_CXX_STANDARD 17)
set(CMAKE_CXX_STANDARD_REQUIRED ON)

if(NOT CMAKE_BUILD_TYPE)
    set(CMAKE_BUILD_TYPE RelWithDebInfo)
endif()

set(CMAKE_CXX_FLAGS_RELWITHDEBINFO "-O2 -g")

find_package(PkgConfig REQUIRED)
pkg_check_modules(FFMPEG REQUIRED IMPORTED_TARGET
    libavformat
    libavcodec
    libavutil
    libswscale
)

add_executable(media-core
    src/main.cpp
    src/media_inspector.cpp
    src/json_report.cpp
)

target_include_directories(media-core PRIVATE
    ${CMAKE_SOURCE_DIR}/third_party
    ${CMAKE_SOURCE_DIR}/src
)

target_link_libraries(media-core PRIVATE PkgConfig::FFMPEG)

target_compile_options(media-core PRIVATE -Wall -Wextra)

# Simple assertion test binary (see tests/README for what it checks)
add_executable(media-core-tests
    tests/test_assertions.cpp
)
target_include_directories(media-core-tests PRIVATE ${CMAKE_SOURCE_DIR}/third_party)
