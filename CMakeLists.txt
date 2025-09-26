cmake_minimum_required(VERSION 3.16)
project(grip_ctrl LANGUAGES CXX C)

set(CMAKE_CXX_STANDARD 17)
set(CMAKE_CXX_STANDARD_REQUIRED ON)

find_package(yaml-cpp REQUIRED)     # YAML parsing
find_package(pybind11 CONFIG QUIET) # Optional, for Python-embedded policy

include_directories(${CMAKE_SOURCE_DIR}/include)
include_directories(${CMAKE_SOURCE_DIR}/vendor)   # contains zcan.h

# Core static lib: configuration, CAN bus, interface, native policy
add_library(grip_core STATIC
  src/settings.cpp
  src/can_bus.cpp
  src/control_interface.cpp
  src/control_policy_native.cpp
)
target_link_libraries(grip_core PRIVATE yaml-cpp)
set_target_properties(grip_core PROPERTIES POSITION_INDEPENDENT_CODE ON)

# Executable using the native C++ policy
add_executable(grip_native src/main_native.cpp)
target_link_libraries(grip_native PRIVATE grip_core)
# If vendor library needs explicit linking, uncomment and adjust:
# target_link_libraries(grip_native PRIVATE usbcanfd)

# Optional Python-embedded policy + executable
if (pybind11_FOUND)
  add_library(grip_py STATIC src/control_policy_python.cpp)
  target_link_libraries(grip_py PRIVATE pybind11::embed)

  add_executable(grip_python src/main_python.cpp)
  target_link_libraries(grip_python PRIVATE grip_core grip_py)
endif()

# Make runtime loader find vendor .so next to binaries
foreach(tgt IN ITEMS grip_native grip_python)
  if (TARGET ${tgt})
    set_target_properties(${tgt} PROPERTIES
      BUILD_RPATH "$ORIGIN"
      INSTALL_RPATH "$ORIGIN"
    )
  endif()
endforeach()

# Installation (optional)
install(TARGETS grip_native RUNTIME DESTINATION bin ARCHIVE DESTINATION lib)
if (TARGET grip_python)
  install(TARGETS grip_python grip_py RUNTIME DESTINATION bin ARCHIVE DESTINATION lib)
endif()
install(FILES config.yaml DESTINATION .)
