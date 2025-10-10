cmake_minimum_required(VERSION 3.16)
project(grip_ctrl LANGUAGES C CXX)

option(BUILD_GUI "Build Qt GUI target" OFF)

set(CMAKE_CXX_STANDARD 17)
set(CMAKE_CXX_STANDARD_REQUIRED ON)

if (MSVC)
  add_compile_options(/W4)
else()
  add_compile_options(-Wall -Wextra -Wpedantic)
endif()

find_package(yaml-cpp REQUIRED)

if (BUILD_GUI)
  set(QT_REQUIRED_COMPONENTS Widgets)
  find_package(Qt6 COMPONENTS ${QT_REQUIRED_COMPONENTS} QUIET)
  if (Qt6_FOUND)
    set(QT_PACKAGE Qt6)
  else()
    find_package(Qt5 COMPONENTS ${QT_REQUIRED_COMPONENTS} REQUIRED)
    set(QT_PACKAGE Qt5)
  endif()
endif()

include_directories(${CMAKE_SOURCE_DIR}/include)
include_directories(${CMAKE_SOURCE_DIR}/vendor)

# Vendor library
if (WIN32)
  set(ZLG_DLL "${CMAKE_SOURCE_DIR}/vendor/usbcanfd.dll")
  set(ZLG_LIB "${CMAKE_SOURCE_DIR}/vendor/usbcanfd.lib")
  add_library(zlgcanfd SHARED IMPORTED GLOBAL)
  set_target_properties(zlgcanfd PROPERTIES
    IMPORTED_LOCATION "${ZLG_DLL}"
    IMPORTED_IMPLIB   "${ZLG_LIB}")
elseif(APPLE)
  set(ZLG_DYLIB "${CMAKE_SOURCE_DIR}/vendor/libusbcanfd.dylib")
  add_library(zlgcanfd SHARED IMPORTED GLOBAL)
  set_target_properties(zlgcanfd PROPERTIES IMPORTED_LOCATION "${ZLG_DYLIB}")
else()
  set(ZLG_SO "${CMAKE_SOURCE_DIR}/vendor/libusbcanfd.so")
  add_library(zlgcanfd SHARED IMPORTED GLOBAL)
  set_target_properties(zlgcanfd PROPERTIES IMPORTED_LOCATION "${ZLG_SO}")
endif()

# Core
add_library(grip_core STATIC
  src/settings.cpp
  src/can_bus.cpp
  src/control_policy_native.cpp
  src/control_interface.cpp
)
target_link_libraries(grip_core PUBLIC yaml-cpp zlgcanfd)
set_target_properties(grip_core PROPERTIES POSITION_INDEPENDENT_CODE ON)

# CLI binary (non-GUI)
add_executable(grip_cli src/main.cpp)
target_link_libraries(grip_cli PRIVATE grip_core)

if (APPLE)
  set(_rpath "@loader_path")
else()
  set(_rpath "$ORIGIN")
endif()
set_target_properties(grip_cli PROPERTIES BUILD_RPATH "${_rpath}" INSTALL_RPATH "${_rpath}")

# GUI binary (optional; uses same control threads)
if (BUILD_GUI)
  set(CMAKE_AUTOMOC ON)
  add_executable(grip_gui
    src_gui/main_gui.cpp
    src_gui/MainWindow.cpp
    src_gui/SidePanel.cpp
    src_gui/TopPanel.cpp
    src_gui/ActionStore.cpp
  )
  target_compile_definitions(grip_gui PRIVATE WITH_QT_GUI=1)
  target_link_libraries(grip_gui PRIVATE grip_core ${QT_PACKAGE}::Widgets)
  set_target_properties(grip_gui PROPERTIES BUILD_RPATH "${_rpath}" INSTALL_RPATH "${_rpath}")
endif()

install(TARGETS grip_cli RUNTIME DESTINATION bin)
if (BUILD_GUI)
  install(TARGETS grip_gui RUNTIME DESTINATION bin)
endif()
install(FILES config.yaml saveData.json DESTINATION .)
