@echo off
setlocal EnableDelayedExpansion
title HotPatch Pusher

set ADB=G:\AndroidSDK\platform-tools\adb.exe
set HOTPATCHER_DIR=G:\TestProject\TestHotpatch\Saved\HotPatcher
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
    set PATCH_VERSION_DIR=%HOTPATCHER_DIR%\%VERSION%
    echo       Using specified version: %VERSION%
) else (
    set LATEST_VERSION=
    for /f "delims=" %%d in ('dir /b /ad /o:n "%HOTPATCHER_DIR%" 2^>nul ^| findstr /r "^[0-9][0-9]*\.[0-9][0-9]*\.[0-9][0-9]*$"') do (
        set LATEST_VERSION=%%d
    )
    if "!LATEST_VERSION!"=="" (
        echo [ERROR] No version directory found in %HOTPATCHER_DIR%
        goto :error
    )
    set VERSION=!LATEST_VERSION!
    set PATCH_VERSION_DIR=%HOTPATCHER_DIR%\!LATEST_VERSION!
    echo       Auto-selected: !LATEST_VERSION!
)

:: Find Android patch dir (Android or Android_ASTC)
echo [3/5] Locating Android patch directory...
set ASTC_DIR=
if exist "%PATCH_VERSION_DIR%\Android_ASTC" (
    set ASTC_DIR=%PATCH_VERSION_DIR%\Android_ASTC
) else if exist "%PATCH_VERSION_DIR%\Android" (
    set ASTC_DIR=%PATCH_VERSION_DIR%\Android
)
if "%ASTC_DIR%"=="" (
    echo [ERROR] No Android/Android_ASTC directory found under %PATCH_VERSION_DIR%
    echo         Did you select Android platform in HotPatcher?
    goto :error
)
echo       OK: %ASTC_DIR%

:: Find .pak file
echo [4/5] Locating .pak file...
set PAK_FILE=
for /f "delims=" %%f in ('dir /b "%ASTC_DIR%\*.pak" 2^>nul') do (
    set PAK_FILE=%%f
)
if "%PAK_FILE%"=="" (
    echo [ERROR] No .pak file found in %ASTC_DIR%
    goto :error
)
set PAK_FULL_PATH=%ASTC_DIR%\%PAK_FILE%
echo       Found: %PAK_FILE%

:: Push
echo [5/5] Pushing to device...
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
