cmake_minimum_required(VERSION 3.21)

project(obs-auto-capture VERSION 1.0.0 LANGUAGES CXX)

set(CMAKE_CXX_STANDARD 17)
set(CMAKE_CXX_STANDARD_REQUIRED ON)
set(CMAKE_CXX_EXTENSIONS OFF)

if(NOT WIN32)
  message(FATAL_ERROR "obs-auto-capture currently supports Windows only.")
endif()

find_package(libobs REQUIRED)

add_library(obs-auto-capture MODULE
  src/plugin-main.cpp
  src/auto-capture-source.cpp
  src/auto-capture-source.hpp
  src/address-bar-locator.cpp
  src/address-bar-locator.hpp
)

target_compile_definitions(obs-auto-capture
  PRIVATE
    UNICODE
    _UNICODE
    NOMINMAX
    WIN32_LEAN_AND_MEAN
)

if(MSVC)
  # Source files are UTF-8; without this MSVC would mangle the localized
  # strings that are compiled into the plugin.
  target_compile_options(obs-auto-capture PRIVATE /utf-8)
endif()

target_include_directories(obs-auto-capture
  PRIVATE
    ${CMAKE_CURRENT_SOURCE_DIR}/src
)

target_link_libraries(obs-auto-capture
  PRIVATE
    OBS::libobs
    dwmapi
    ole32
    oleaut32
    uuid
)

set_target_properties(obs-auto-capture PROPERTIES
  PREFIX ""
  OUTPUT_NAME "obs-auto-capture"
)

install(TARGETS obs-auto-capture RUNTIME DESTINATION obs-plugins/64bit LIBRARY DESTINATION obs-plugins/64bit)
install(DIRECTORY data/locale DESTINATION data/obs-plugins/obs-auto-capture)
install(DIRECTORY data/shaders DESTINATION data/obs-plugins/obs-auto-capture)
