@echo off
setlocal
title TeamSync

rem Always run from the project directory so data and reports stay here.
pushd "%~dp0" >nul || (
    echo TeamSync could not open its project folder.
    pause
    exit /b 1
)

:choose_mode
cls
echo ==================================================
echo                     TEAMSYNC
echo ==================================================
echo.
echo   1. GUI application  - buttons, forms, and dashboard
echo   2. Console mode     - classic text interface
echo   3. Exit
echo.
set "TEAMSYNC_MODE="
set /p "TEAMSYNC_MODE=Choose how you want to use TeamSync [1-3]: "

if "%TEAMSYNC_MODE%"=="1" goto run_gui
if "%TEAMSYNC_MODE%"=="2" goto run_console
if "%TEAMSYNC_MODE%"=="3" goto close_launcher
echo.
echo Please enter 1, 2, or 3.
pause
goto choose_mode

:run_gui
set "TEAMSYNC_GUI=%~dp0build\gui\teamsync_gui.exe"
if not exist "%TEAMSYNC_GUI%" (
    echo.
    echo The GUI application has not been built yet.
    echo Expected: build\gui\teamsync_gui.exe
    pause
    goto choose_mode
)
start "TeamSync" /D "%~dp0" "%TEAMSYNC_GUI%" --data-dir "%~dp0."
popd
exit /b 0

:run_console
set "TEAMSYNC_EXE=%~dp0build\teamsync.exe"
if not exist "%TEAMSYNC_EXE%" set "TEAMSYNC_EXE=%~dp0build-verify\teamsync.exe"
if not exist "%TEAMSYNC_EXE%" (
    echo.
    echo The console application has not been built yet.
    echo Expected: build\teamsync.exe
    pause
    goto choose_mode
)
cls
"%TEAMSYNC_EXE%" --data-dir "%~dp0."
set "TEAMSYNC_EXIT=%ERRORLEVEL%"

if not "%TEAMSYNC_EXIT%"=="0" (
    echo.
    echo TeamSync stopped with error code %TEAMSYNC_EXIT%.
    pause
)

popd
exit /b %TEAMSYNC_EXIT%

:close_launcher
popd
exit /b 0
