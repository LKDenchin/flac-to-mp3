cmake_minimum_required(VERSION 3.20)
project(FlacToMp3Converter LANGUAGES C CXX)

set(CMAKE_CXX_STANDARD 20)
set(CMAKE_CXX_STANDARD_REQUIRED ON)
set(CMAKE_EXPORT_COMPILE_COMMANDS ON)

set(CMAKE_AUTOMOC ON)
set(CMAKE_AUTOUIC ON)
set(CMAKE_AUTORCC ON)

find_package(PkgConfig REQUIRED)
pkg_check_modules(FFMPEG REQUIRED IMPORTED_TARGET
    libavformat
    libavcodec
    libswresample
    libavutil
)

pkg_check_modules(TAGLIB REQUIRED IMPORTED_TARGET
    taglib
)

find_package(Qt6 REQUIRED COMPONENTS Widgets)

add_executable(${PROJECT_NAME}
    src/main.cpp
    src/thread_pool.cpp
    src/scanner_agent.cpp
    src/transcoder_agent.cpp
    src/metadata_agent.cpp
    src/coordinator.cpp
    ui/main_window.cpp
)

target_include_directories(${PROJECT_NAME} PRIVATE 
    ${CMAKE_CURRENT_SOURCE_DIR}/include
    ${CMAKE_CURRENT_SOURCE_DIR}/ui
)

target_link_libraries(${PROJECT_NAME} PRIVATE
    PkgConfig::FFMPEG
    PkgConfig::TAGLIB
    Qt6::Widgets
    pthread
)

if (MSVC)
    target_compile_options(${PROJECT_NAME} PRIVATE /W4 /permissive-)
else()
    target_compile_options(${PROJECT_NAME} PRIVATE -Wall -Wextra -Wpedantic)
endif()
