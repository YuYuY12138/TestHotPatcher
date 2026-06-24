@echo off
setlocal EnableDelayedExpansion
title HotPatch Pusher

set ADB=G:\AndroidSDK\platform-tools\adb.exe
set PATCHES_DIR=G:\TestProject\TestHotpatch\Saved\HotPatcher\Patches
set PHONE_PENDING=/storage/emulated/0/Android/data/com.YourCompany.TestHotpatch/files/UnrealGame/TestHotpatch/TestHotpatch/Saved/HotUpdate/Pending

echo ========================================
echo  HotPatch Pusher
echo ========================================
echo.

:: Check adb
if not exist "%ADB%" (
    echo [ERROR] adb not found: %ADB%
    goto :error
)

:: Check device
echo [1/5] Checking device connection...
"%ADB%" devices 2>nul | findstr /i "device" | findstr /v "List" >nul
if errorlevel 1 (
    echo [ERROR] No Android device detected.
    echo         Make sure USB debugging is enabled.
    goto :error
)
echo       OK

:: Find version directory
echo [2/5] Locating patch version...
if not "%1"=="" (
    set VERSION=%1
    set PATCH_VERSION_DIR=%PATCHES_DIR%\%VERSION%
    echo       Using specified version: %VERSION%
) else (
    set LATEST_VERSION=
    for /f "delims=" %%d in ('dir /b /ad /o:n "%PATCHES_DIR%" 2^>nul ^| findstr /r "^[0-9][0-9]*\.[0-9][0-9]*\.[0-9][0-9]*$"') do (
        set LATEST_VERSION=%%d
    )
    if "!LATEST_VERSION!"=="" (
        echo [ERROR] No version directory found in %PATCHES_DIR%
        goto :error
    )
    set VERSION=!LATEST_VERSION!
    set PATCH_VERSION_DIR=%PATCHES_DIR%\!LATEST_VERSION!
    echo       Auto-selected: !LATEST_VERSION!
)

:: Find .pak file recursively under the version directory
echo [3/5] Locating .pak file...
set PAK_FULL_PATH=
for /f "delims=" %%f in ('dir /s /b "%PATCH_VERSION_DIR%\*.pak" 2^>nul') do (
    set PAK_FULL_PATH=%%f
)
if "%PAK_FULL_PATH%"=="" (
    echo [ERROR] No .pak file found under %PATCH_VERSION_DIR%
    echo         Did you build the patch successfully?
    goto :error
)
for %%f in ("%PAK_FULL_PATH%") do set PAK_FILE=%%~nxf
echo       Found: %PAK_FULL_PATH%

:: Push
echo [4/5] Pushing to device...
echo.
echo   File : %PAK_FILE%
echo   To   : %PHONE_PENDING%/
echo.

"%ADB%" shell mkdir -p "%PHONE_PENDING%" >nul 2>&1
"%ADB%" push "%PAK_FULL_PATH%" "%PHONE_PENDING%/"

if errorlevel 1 (
    echo.
    echo [ERROR] adb push failed.
    goto :error
)

echo.
echo ========================================
echo  SUCCESS!
echo  Version : %VERSION%
echo  File    : %PAK_FILE%
echo  Launch the game and click Enter Game.
echo ========================================
echo.
echo Closing in 10 seconds...
timeout /t 10
exit /b 0

:error
echo.
echo [FAILED] See error above.
echo.
pause
exit /b 1
