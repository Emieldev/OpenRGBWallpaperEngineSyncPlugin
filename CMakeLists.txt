cmake_minimum_required(VERSION 3.16)
project(OpenRGBWallpaperEngineSyncPlugin VERSION 1.0.0 LANGUAGES CXX)

set(CMAKE_CXX_STANDARD 17)
set(CMAKE_CXX_STANDARD_REQUIRED ON)
set(CMAKE_AUTOMOC ON)

# Find Qt (Prefer Qt6, fallback to Qt5)
find_package(Qt6 COMPONENTS Core Widgets Gui QUIET)
if(Qt6_FOUND)
    set(QT_VERSION_MAJOR 6)
else()
    find_package(Qt5 COMPONENTS Core Widgets Gui REQUIRED)
    set(QT_VERSION_MAJOR 5)
endif()

set(PLUGIN_SOURCES
    OpenRGBWallpaperEnginePlugin.cpp
    OpenRGBWallpaperEnginePlugin.h
    OpenRGBPluginInterface.h
    RGBControllerInterface.h
    RGBController/RGBController.h
    filesystem.h
)

add_library(${PROJECT_NAME} SHARED ${PLUGIN_SOURCES})

target_include_directories(${PROJECT_NAME} PRIVATE
    ${CMAKE_CURRENT_SOURCE_DIR}
    ${CMAKE_CURRENT_SOURCE_DIR}/RGBController
    ${CMAKE_CURRENT_SOURCE_DIR}/nlohmann
)

target_link_libraries(${PROJECT_NAME} PRIVATE
    Qt${QT_VERSION_MAJOR}::Core
    Qt${QT_VERSION_MAJOR}::Widgets
    Qt${QT_VERSION_MAJOR}::Gui
)

if(WIN32)
    target_link_libraries(${PROJECT_NAME} PRIVATE user32 gdi32 advapi32)
else()
    target_link_libraries(${PROJECT_NAME} PRIVATE rt pthread)
endif()

# Output settings
set_target_properties(${PROJECT_NAME} PROPERTIES
    PREFIX ""
    OUTPUT_NAME "OpenRGBWallpaperEngineSyncPlugin"
)

