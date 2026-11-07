@echo off
echo Cleaning build artifacts...
del /q /f *.obj *.exp *.lib *.ilk *.pdb moc_*.cpp *.zip 2>nul
if exist OpenRGBWallpaperEngineSyncPlugin.dll del /q /f OpenRGBWallpaperEngineSyncPlugin.dll 2>nul
echo Clean complete.

