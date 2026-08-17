cmake_minimum_required(VERSION 3.28...3.30)

include("${CMAKE_CURRENT_SOURCE_DIR}/cmake/common/bootstrap.cmake" NO_POLICY_SCOPE)

project(${_name} VERSION ${_version})

if(NOT OS_WINDOWS)
  message(FATAL_ERROR "obs-auto-capture supports Windows only: it is built on Win32 window management and UI Automation.")
endif()

# The settings window needs both. With either one off the plugin still builds
# and works, only without the window: the source properties keep every setting.
option(ENABLE_FRONTEND_API "Use obs-frontend-api for UI functionality" ON)
option(ENABLE_QT "Use Qt functionality" ON)

include(compilerconfig)
include(defaults)
include(helpers)

add_library(${CMAKE_PROJECT_NAME} MODULE)

find_package(libobs REQUIRED)
target_link_libraries(${CMAKE_PROJECT_NAME} PRIVATE OBS::libobs)

if(ENABLE_FRONTEND_API)
  find_package(obs-frontend-api REQUIRED)
  target_link_libraries(${CMAKE_PROJECT_NAME} PRIVATE OBS::obs-frontend-api)
endif()

if(ENABLE_QT)
  find_package(Qt6 COMPONENTS Widgets Core)
  target_link_libraries(${CMAKE_PROJECT_NAME} PRIVATE Qt6::Core Qt6::Widgets)
  set_target_properties(${CMAKE_PROJECT_NAME} PROPERTIES AUTOMOC ON AUTOUIC ON AUTORCC ON)
endif()

target_sources(
  ${CMAKE_PROJECT_NAME}
  PRIVATE
    src/plugin-main.cpp
    src/auto-capture-source.cpp
    src/auto-capture-source.hpp
    src/address-bar-locator.cpp
    src/address-bar-locator.hpp
)

if(ENABLE_QT AND ENABLE_FRONTEND_API)
  target_sources(
    ${CMAKE_PROJECT_NAME}
    PRIVATE
      src/settings-window.cpp
      src/settings-window.hpp
      src/applications-page.cpp
      src/applications-page.hpp
      src/settings-pages.cpp
      src/settings-pages.hpp
      src/source-preview.cpp
      src/source-preview.hpp
      src/plugin-api.hpp
  )
  target_compile_definitions(${CMAKE_PROJECT_NAME} PRIVATE AUTO_CAPTURE_HAS_UI)
endif()

target_include_directories(${CMAKE_PROJECT_NAME} PRIVATE "${CMAKE_CURRENT_SOURCE_DIR}/src")

target_compile_features(${CMAKE_PROJECT_NAME} PRIVATE cxx_std_17)

target_compile_definitions(
  ${CMAKE_PROJECT_NAME}
  PRIVATE
    UNICODE
    _UNICODE
    NOMINMAX
    WIN32_LEAN_AND_MEAN
)

# dwmapi: the window rectangle the capture is aligned to.
# ole32/oleaut32/uuid: UI Automation is a COM API.
target_link_libraries(
  ${CMAKE_PROJECT_NAME}
  PRIVATE
    dwmapi
    ole32
    oleaut32
    uuid
)

set_target_properties_plugin(${CMAKE_PROJECT_NAME} PROPERTIES OUTPUT_NAME ${_name})
