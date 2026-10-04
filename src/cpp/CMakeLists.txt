cmake_minimum_required(VERSION 3.16)
project(neonify LANGUAGES CXX)

set(CMAKE_CXX_STANDARD 17)
set(CMAKE_CXX_STANDARD_REQUIRED ON)
cmake_policy(SET CMP0091 NEW)
set(CMAKE_MSVC_RUNTIME_LIBRARY "MultiThreaded$<$<CONFIG:Debug>:Debug>")

option(NEONIFY_GUI_ONLY_SKIP "build the cli engine without qt (local testing)" OFF)

if(NOT CMAKE_BUILD_TYPE)
  set(CMAKE_BUILD_TYPE Release)
endif()

set(CMAKE_AUTOMOC ON)
set(CMAKE_AUTORCC ON)
set(CMAKE_AUTOUIC ON)

if(NOT NEONIFY_GUI_ONLY_SKIP)
  find_package(Qt5 REQUIRED COMPONENTS Core Gui Widgets Multimedia MultimediaWidgets)
endif()
find_package(OpenCV REQUIRED COMPONENTS core imgproc imgcodecs)

set(NEON_QT_STATIC OFF)
if(NOT NEONIFY_GUI_ONLY_SKIP)
  get_target_property(_neon_qt_core_type Qt5::Core TYPE)
  if(_neon_qt_core_type STREQUAL "STATIC_LIBRARY")
    set(NEON_QT_STATIC ON)
  endif()
endif()

if(NEONIFY_GUI_ONLY_SKIP)
  add_executable(neonify src/neon_cli.cpp)
else()
  set(NEON_SOURCES src/neon_cli.cpp src/neon_gui.cpp neon.qrc)
  if(WIN32)
    add_executable(neonify WIN32 ${NEON_SOURCES} app.rc)
  else()
    add_executable(neonify ${NEON_SOURCES})
  endif()
endif()

target_include_directories(neonify PRIVATE
  ${CMAKE_CURRENT_SOURCE_DIR}/src
  ${CMAKE_CURRENT_SOURCE_DIR}
)

if(NOT NEONIFY_GUI_ONLY_SKIP)
  target_link_libraries(neonify PRIVATE
    Qt5::Core Qt5::Gui Qt5::Widgets Qt5::Multimedia Qt5::MultimediaWidgets
    ${OpenCV_LIBS}
  )
  if(NEON_QT_STATIC)
    target_compile_definitions(neonify PRIVATE NEONIFY_STATIC_QT)
  endif()
  target_compile_definitions(neonify PRIVATE NEONIFY_WITH_GUI)
else()
  target_link_libraries(neonify PRIVATE ${OpenCV_LIBS})
endif()

if(MSVC)
  target_compile_options(neonify PRIVATE /EHsc /utf-8)
  target_link_options(neonify PRIVATE /SUBSYSTEM:WINDOWS /ENTRY:mainCRTStartup)
else()
  target_compile_options(neonify PRIVATE -O2)
endif()

# ---------------------------------------------------------------- desktop neonifier
# qt builds only — the gui-skip engine build has no overlay target
if(NOT NEONIFY_GUI_ONLY_SKIP)
  set(NEONIFY_DESKTOP_SOURCES src/neon_desktop.cpp neon.qrc)
  if(WIN32)
    add_executable(neonify-desktop WIN32 ${NEONIFY_DESKTOP_SOURCES} app.rc)
  else()
    add_executable(neonify-desktop ${NEONIFY_DESKTOP_SOURCES})
  endif()
  target_include_directories(neonify-desktop PRIVATE
    ${CMAKE_CURRENT_SOURCE_DIR}/src
    ${CMAKE_CURRENT_SOURCE_DIR}
  )
  target_link_libraries(neonify-desktop PRIVATE Qt5::Core Qt5::Gui Qt5::Widgets ${OpenCV_LIBS})
  if(NEON_QT_STATIC)
    target_compile_definitions(neonify-desktop PRIVATE NEONIFY_STATIC_QT)
  endif()
  if(MSVC)
    target_compile_options(neonify-desktop PRIVATE /EHsc /utf-8)
    target_link_options(neonify-desktop PRIVATE /SUBSYSTEM:WINDOWS /ENTRY:mainCRTStartup)
    target_link_libraries(neonify-desktop PRIVATE d3d11 dxgi user32)
  else()
    target_compile_options(neonify-desktop PRIVATE -O2)
    find_package(X11 REQUIRED)
    target_compile_definitions(neonify-desktop PRIVATE NEONIFY_WITH_X11)
    target_include_directories(neonify-desktop PRIVATE ${X11_INCLUDE_DIR})
    target_link_libraries(neonify-desktop PRIVATE ${X11_LIBRARIES} Xext)
    find_package(PkgConfig QUIET)
    if(PkgConfig_FOUND)
      pkg_check_modules(PIPEWIRE IMPORTED_TARGET libpipewire-0.3)
      pkg_check_modules(SDBUS IMPORTED_TARGET libsystemd)
      if(PIPEWIRE_FOUND AND SDBUS_FOUND)
        target_compile_definitions(neonify-desktop PRIVATE NEONIFY_WITH_PORTAL)
        target_link_libraries(neonify-desktop PRIVATE PkgConfig::PIPEWIRE PkgConfig::SDBUS)
      endif()
    endif()
  endif()
  install(TARGETS neonify neonify-desktop RUNTIME DESTINATION .)
else()
  install(TARGETS neonify RUNTIME DESTINATION .)
endif()
