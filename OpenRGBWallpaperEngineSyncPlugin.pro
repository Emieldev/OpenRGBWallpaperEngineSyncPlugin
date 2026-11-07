#-------------------------------------------------
#
# Project created for OpenRGB Wallpaper Engine Sync Plugin
#
#-------------------------------------------------

QT       += core gui widgets

TARGET    = OpenRGBWallpaperEngineSyncPlugin
TEMPLATE  = lib
CONFIG   += plugin c++17

DEFINES  += QT_DEPRECATED_WARNINGS

HEADERS  += \
    OpenRGBWallpaperEnginePlugin.h \
    OpenRGBPluginInterface.h \
    RGBControllerInterface.h \
    RGBController/RGBController.h \
    filesystem.h

SOURCES  += \
    OpenRGBWallpaperEnginePlugin.cpp

INCLUDEPATH += . RGBController nlohmann

win32 {
    LIBS += -luser32 -lgdi32 -ladvapi32
} else {
    LIBS += -lrt -lpthread
}

DISTFILES += \
    OpenRGBWallpaperEnginePlugin.json \
    README.md \
    LICENSE

