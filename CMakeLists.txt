cmake_minimum_required(VERSION 3.20)
project(grip_ctrl LANGUAGES C CXX)

# option(BUILD_CAMERA "Build Qt GUI target" OFF)
option(BUILD_UDE_GLOVE "Build Qt UDE Glove drive" OFF)
option(BUILD_MANUS_GLOVE "Build Manus Glove drive. Need pre-install Manus Glove requirements" OFF)

set(CMAKE_CXX_STANDARD 17)
set(CMAKE_CXX_STANDARD_REQUIRED ON)
set(CMAKE_POSITION_INDEPENDENT_CODE ON)

# set(CMAKE_AUTOMOC ON)

if (MSVC)
  add_compile_options(/W4)
else()
  add_compile_options(-Wall -Wextra -Wpedantic)
endif()

find_package(yaml-cpp REQUIRED)
find_package(Python3 COMPONENTS Interpreter Development REQUIRED)
find_package(OGRE REQUIRED CONFIG)
find_package(assimp REQUIRED CONFIG)


# ---- OpenCV (only for color utilities; not strictly needed if you keep manual BGR->RGB) ----
find_package(OpenCV QUIET COMPONENTS core imgproc)

# ---- pybind11 (CONFIG mode; e.g., pip install pybind11) ----
find_package(pybind11 CONFIG REQUIRED)

set(QT_REQUIRED_COMPONENTS Widgets)
find_package(Qt5 COMPONENTS ${QT_REQUIRED_COMPONENTS} REQUIRED)
find_package(Qt5Charts REQUIRED)
set(QT_PACKAGE Qt5)

if (BUILD_UDE_GLOVE)
  include_directories(${CMAKE_SOURCE_DIR}/include)
  include_directories(${CMAKE_SOURCE_DIR}/third_party/HandDriver)
  include_directories(${CMAKE_SOURCE_DIR}/third_party/HandDriver/include/json)

  add_subdirectory(${CMAKE_SOURCE_DIR}/third_party/HandDriver/include)
  set(HANDDRIVER_LIB_DIR "${CMAKE_SOURCE_DIR}/third_party/HandDriver/lib")

  find_library(HANDDRIVER_JSONCPP_LIBRARY
    NAMES jsoncpp
    PATHS "${HANDDRIVER_LIB_DIR}"
    NO_DEFAULT_PATH
    REQUIRED
  )
endif()

if (BUILD_MANUS_GLOVE)
  include_directories(${CMAKE_SOURCE_DIR}/third_party/SDKClient_Linux)
  set(HANDDRIVER_LIB_DIR "${CMAKE_SOURCE_DIR}/third_party/SDKClient_Linux/ManusSDK/lib")

  find_library(MANUS_LIBRARY
    NAMES ManusSDK_Integrated
    PATHS "${HANDDRIVER_LIB_DIR}"
    NO_DEFAULT_PATH
    REQUIRED
  )

  find_package(Curses REQUIRED)
  include_directories(${CURSES_INCLUDE_DIR})
endif()

# Vendor library
set(TPHAND_SO "${CMAKE_SOURCE_DIR}/lib/libtphandapi.so")
add_library(tphandapi SHARED IMPORTED GLOBAL)
set_target_properties(tphandapi PROPERTIES IMPORTED_LOCATION "${TPHAND_SO}")

# Core
add_library(grip_core STATIC
  src/settings.cpp
  src/control_interface.cpp
)
target_link_libraries(grip_core PUBLIC yaml-cpp tphandapi)
set_target_properties(grip_core PROPERTIES POSITION_INDEPENDENT_CODE ON)
target_include_directories(grip_core PRIVATE include)

set(_rpath "$ORIGIN")

# GUI binary
set(CMAKE_AUTOMOC ON)
set(GUI_SOURCES
  src_gui/main_gui.cpp
  src_gui/MainWindow.cpp
  src_gui/SidePanel.cpp
  src_gui/ActionStore.cpp
  src_gui/OgreWidget.cpp
  src_gui/UrdfLoader.cpp
  src_gui/MotorPanel.cpp
  src_gui/Connector.cpp
  src_gui/CameraWidget.cpp
  src_gui/DraggableTabBar.cpp
  src_gui/DraggableTabWidget.cpp
  src_gui/ActionPanel.cpp
  src_gui/DiagramWidget.cpp
  src_gui/PlotPanel.cpp
  src_gui/TactileVisual.cpp
  src_gui/LedIndicator.cpp
  src_gui/CameraHandDetect.cpp
  src_gui/TeleOperation.hpp
)

if (BUILD_UDE_GLOVE)
  list(APPEND GUI_SOURCES
    src_gui/UDETeleop.cpp
    third_party/HandDriver/UDEServer.cxx
  )
endif()

if (BUILD_MANUS_GLOVE)
  list(APPEND GUI_SOURCES
    src_gui/ManusTeleop.cpp
    third_party/SDKClient_Linux/ClientPlatformSpecific.cpp
  )
endif()

add_executable(grip_gui ${GUI_SOURCES})
target_include_directories(grip_gui PRIVATE ${OpenCV_INCLUDE_DIRS})
target_include_directories(grip_gui PRIVATE ${OGRE_INCLUDE_DIRS})
target_link_libraries(grip_gui PRIVATE Python3::Python)
target_compile_definitions(grip_gui PRIVATE WITH_QT_GUI=1)
target_link_libraries(grip_gui PRIVATE
  grip_core
  ${QT_PACKAGE}::Widgets
  pybind11::embed
  ${OpenCV_LIBS}
  ${OGRE_LIBRARIES}
  assimp::assimp
  tinyxml2
  ${QT_PACKAGE}::Charts
)

if (BUILD_UDE_GLOVE)
  target_link_libraries(grip_gui PRIVATE ${HANDDRIVER_JSONCPP_LIBRARY})
  target_compile_definitions(grip_gui PRIVATE USE_UDE_TELEOP)
endif()

if (BUILD_MANUS_GLOVE)
  target_link_libraries(grip_gui PRIVATE ${MANUS_LIBRARY} ${CURSES_LIBRARIES})
  target_compile_definitions(grip_gui PRIVATE USE_MANUS_TELEOP)
endif()

target_include_directories(grip_gui PRIVATE include)

# target_link_libraries(grip_gui PRIVATE grip_core ${QT_PACKAGE}::Widgets)
set_target_properties(grip_gui PROPERTIES
  BUILD_RPATH "${_rpath};${HANDDRIVER_LIB_DIR}"
  INSTALL_RPATH "${_rpath};${HANDDRIVER_LIB_DIR}"
)

# install(TARGETS grip_cli RUNTIME DESTINATION bin)
install(TARGETS grip_gui RUNTIME DESTINATION bin)
install(FILES config.yaml saveData.json DESTINATION .)
