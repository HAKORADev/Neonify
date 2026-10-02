cmake_minimum_required(VERSION 3.16)
project(neonify VERSION 0.5.0 LANGUAGES CXX)

set(CMAKE_CXX_STANDARD 17)
set(CMAKE_CXX_STANDARD_REQUIRED ON)
set(CMAKE_AUTOMOC ON)

if (NOT CMAKE_BUILD_TYPE)
    set(CMAKE_BUILD_TYPE Release)
endif()
set(CMAKE_INTERPROCEDURAL_OPTIMIZATION_RELEASE OFF)

find_package(Qt5 5.15 REQUIRED COMPONENTS Core Gui Widgets)
find_package(OpenCV REQUIRED COMPONENTS core imgproc imgcodecs)

add_executable(neonify
    src/neon_cli.cpp
    src/neon_gui.cpp
)
target_include_directories(neonify PRIVATE ${CMAKE_CURRENT_SOURCE_DIR}/src)
target_compile_definitions(neonify PRIVATE NEONIFY_WITH_GUI)
set_target_properties(neonify PROPERTIES
    BUILD_RPATH "\$ORIGIN/lib"
    INSTALL_RPATH "\$ORIGIN/lib")
target_link_libraries(neonify PRIVATE Qt5::Core Qt5::Gui Qt5::Widgets ${OpenCV_LIBS})

if (MSVC)
    target_compile_options(neonify PRIVATE /O2 /EHsc /W3)
    target_compile_definitions(neonify PRIVATE _CRT_SECURE_NO_WARNINGS NOMINMAX)
else()
    target_compile_options(neonify PRIVATE -O2 -Wall -Wextra -Wno-unused-parameter)
endif()
