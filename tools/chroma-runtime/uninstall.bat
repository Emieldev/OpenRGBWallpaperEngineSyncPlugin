@echo off
setlocal enabledelayedexpansion

echo =====================================================================
echo   Razer Chroma Runtime Standalone Uninstaller
echo =====================================================================
echo.

net session >nul 2>&1
if %errorLevel% neq 0 (
    echo [!] Administrator permissions are required to remove DLLs from System32.
    echo Requesting elevation...
    powershell -Command "Start-Process '%~f0' -Verb RunAs"
    exit /b
)

echo [*] Removing RzChromaSDK64.dll from %SystemRoot%\System32...
if exist "%SystemRoot%\System32\RzChromaSDK64.dll" del /f /q "%SystemRoot%\System32\RzChromaSDK64.dll"

echo [*] Removing RzChromaSDK.dll from %SystemRoot%\SysWOW64...
if exist "%SystemRoot%\SysWOW64\RzChromaSDK.dll" del /f /q "%SystemRoot%\SysWOW64\RzChromaSDK.dll"

echo [*] Removing RzChromaSDK.dll from %SystemRoot%\System32...
if exist "%SystemRoot%\System32\RzChromaSDK.dll" del /f /q "%SystemRoot%\System32\RzChromaSDK.dll"

echo.
echo [OK] Chroma runtime uninstalled.
echo.
pause
