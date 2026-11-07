@echo off
setlocal enabledelayedexpansion

echo =====================================================================
echo   Razer Chroma Runtime Standalone Installer for Wallpaper Engine
echo =====================================================================
echo.
echo This script installs the official Razer Chroma SDK runtime DLLs
echo required by Wallpaper Engine to emit real-time RGB lighting streams.
echo.
echo NOTE: You DO NOT need any physical Razer hardware connected.
echo.

:: Check for Administrator permissions
net session >nul 2>&1
if %errorLevel% neq 0 (
    echo [!] Administrator permissions are required to copy DLLs to System32.
    echo Requesting elevation...
    powershell -Command "Start-Process '%~f0' -Verb RunAs"
    exit /b
)

set "SCRIPT_DIR=%~dp0"
set "X64_DLL=%SCRIPT_DIR%x64\RzChromaSDK64.dll"
set "X86_DLL=%SCRIPT_DIR%x86\RzChromaSDK.dll"

if not exist "%X64_DLL%" (
    echo [ERROR] %X64_DLL% not found!
    pause
    exit /b 1
)

echo [*] Installing 64-bit runtime (RzChromaSDK64.dll) to %SystemRoot%\System32...
copy /y "%X64_DLL%" "%SystemRoot%\System32\" >nul
if %errorLevel% equ 0 (
    echo     [OK] 64-bit runtime installed.
) else (
    echo     [FAILED] Could not copy to %SystemRoot%\System32.
)

if exist "%SystemRoot%\SysWOW64" (
    echo [*] 64-bit Windows detected. Installing 32-bit runtime (RzChromaSDK.dll) to %SystemRoot%\SysWOW64...
    copy /y "%X86_DLL%" "%SystemRoot%\SysWOW64\" >nul
    if %errorLevel% equ 0 (
        echo     [OK] 32-bit runtime installed.
    ) else (
        echo     [FAILED] Could not copy to %SystemRoot%\SysWOW64.
    )
) else (
    echo [*] 32-bit Windows detected. Installing 32-bit runtime (RzChromaSDK.dll) to %SystemRoot%\System32...
    copy /y "%X86_DLL%" "%SystemRoot%\System32\" >nul
    if %errorLevel% equ 0 (
        echo     [OK] 32-bit runtime installed.
    ) else (
        echo     [FAILED] Could not copy to %SystemRoot%\System32.
    )
)

:: Optional: Also copy directly into Wallpaper Engine directory if found
set "WE_PATHS="
if exist "%ProgramFiles(x86)%\Steam\steamapps\common\wallpaper_engine" set "WE_PATH=%ProgramFiles(x86)%\Steam\steamapps\common\wallpaper_engine"
if exist "%ProgramFiles%\Steam\steamapps\common\wallpaper_engine" set "WE_PATH=%ProgramFiles%\Steam\steamapps\common\wallpaper_engine"

if defined WE_PATH (
    echo [*] Detected Wallpaper Engine at: %WE_PATH%
    copy /y "%X64_DLL%" "%WE_PATH%\bin\" >nul 2>&1
    copy /y "%X64_DLL%" "%WE_PATH%\" >nul 2>&1
    copy /y "%X86_DLL%" "%WE_PATH%\" >nul 2>&1
    echo     [OK] Added local runtime copies to Wallpaper Engine folder.
)

echo.
echo =====================================================================
echo   Installation Complete!
echo =====================================================================
echo.
echo Next steps:
echo 1. Open Wallpaper Engine Settings -^> Plugins.
echo 2. Ensure "iCUE ^& Chroma SDK" is enabled (checked).
echo 3. Start or restart OpenRGB and enable Wallpaper Engine Lighting Sync!
echo.
pause
