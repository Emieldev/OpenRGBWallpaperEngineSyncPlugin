@echo off
setlocal enabledelayedexpansion

echo ===================================================
echo Building OpenRGBWallpaperEngineSyncPlugin.dll (Release)
echo ===================================================

if "%VCVARS_BAT%"=="" (
    if exist "C:\Program Files (x86)\Microsoft Visual Studio\2019\Community\VC\Auxiliary\Build\vcvars64.bat" (
        set "VCVARS_BAT=C:\Program Files (x86)\Microsoft Visual Studio\2019\Community\VC\Auxiliary\Build\vcvars64.bat"
    ) else if exist "C:\Program Files\Microsoft Visual Studio\2022\Community\VC\Auxiliary\Build\vcvars64.bat" (
        set "VCVARS_BAT=C:\Program Files\Microsoft Visual Studio\2022\Community\VC\Auxiliary\Build\vcvars64.bat"
    ) else if exist "C:\Program Files (x86)\Microsoft Visual Studio\2019\BuildTools\VC\Auxiliary\Build\vcvars64.bat" (
        set "VCVARS_BAT=C:\Program Files (x86)\Microsoft Visual Studio\2019\BuildTools\VC\Auxiliary\Build\vcvars64.bat"
    )
)

if not "%VCVARS_BAT%"=="" (
    call "%VCVARS_BAT%"
)

if "%QT_DIR%"=="" (
    if not "%QTDIR%"=="" (
        set "QT_DIR=%QTDIR%"
    ) else (
        for %%D in (C D E F G H) do (
            if exist "%%D:\Qt\6.8.3\msvc2022_64" set "QT_DIR=%%D:\Qt\6.8.3\msvc2022_64"
            if exist "%%D:\Qt\6.8.2\msvc2022_64" set "QT_DIR=%%D:\Qt\6.8.2\msvc2022_64"
            if exist "%%D:\Qt\6.5.3\msvc2022_64" set "QT_DIR=%%D:\Qt\6.5.3\msvc2022_64"
        )
    )
)

set MOC=%QT_DIR%\bin\moc.exe

echo Running moc...
"%MOC%" -DQT_NO_DEBUG -DNDEBUG -I"%QT_DIR%\include" -I"%QT_DIR%\include\QtCore" -I"%QT_DIR%\include\QtWidgets" -I"%QT_DIR%\include\QtGui" -I. OpenRGBWallpaperEnginePlugin.h -o moc_OpenRGBWallpaperEnginePlugin.cpp
if errorlevel 1 (
    echo moc failed!
    exit /b 1
)

echo Compiling C++ files in RELEASE mode...
cl /std:c++17 /Zc:__cplusplus /permissive- /EHsc /MD /O2 /W3 /DQT_NO_DEBUG /DNDEBUG ^
   /I. /I"%QT_DIR%\include" /I"%QT_DIR%\include\QtCore" /I"%QT_DIR%\include\QtWidgets" /I"%QT_DIR%\include\QtGui" ^
   /c OpenRGBWallpaperEnginePlugin.cpp moc_OpenRGBWallpaperEnginePlugin.cpp
if errorlevel 1 (
    echo Compilation failed!
    exit /b 1
)

echo Linking OpenRGBWallpaperEngineSyncPlugin.dll...
link /DLL /OUT:OpenRGBWallpaperEngineSyncPlugin.dll ^
     OpenRGBWallpaperEnginePlugin.obj moc_OpenRGBWallpaperEnginePlugin.obj ^
     /LIBPATH:"%QT_DIR%\lib" Qt6Core.lib Qt6Widgets.lib Qt6Gui.lib user32.lib gdi32.lib advapi32.lib
if errorlevel 1 (
    echo Linking failed!
    exit /b 1
)

echo ===================================================
echo Successfully built OpenRGBWallpaperEngineSyncPlugin.dll!
echo ===================================================

echo Creating zipped release asset...
powershell -NoProfile -Command "Compress-Archive -Path OpenRGBWallpaperEngineSyncPlugin.dll, README.md, LICENSE -DestinationPath OpenRGBWallpaperEngineSyncPlugin-windows-x64.zip -Force"
if exist OpenRGBWallpaperEngineSyncPlugin-windows-x64.zip (
    echo Zipped release asset created: OpenRGBWallpaperEngineSyncPlugin-windows-x64.zip
)
if exist "tools\chroma-runtime" (
    powershell -NoProfile -Command "Compress-Archive -Path tools\chroma-runtime\* -DestinationPath razer-chroma-runtime-standalone.zip -Force"
    if exist razer-chroma-runtime-standalone.zip (
        echo Zipped release asset created: razer-chroma-runtime-standalone.zip
    )
)
