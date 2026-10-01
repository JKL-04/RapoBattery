@echo off
REM ============================================================
REM  RapooBattery - build script
REM  Requires Visual Studio with the C++ toolchain and the Windows SDK.
REM ============================================================
setlocal

cd /d "%~dp0"

set "VCVARS="

if exist "%ProgramFiles%\Microsoft Visual Studio\18\BuildTools\VC\Auxiliary\Build\vcvars64.bat" set "VCVARS=%ProgramFiles%\Microsoft Visual Studio\18\BuildTools\VC\Auxiliary\Build\vcvars64.bat"
if not defined VCVARS if exist "%ProgramFiles%\Microsoft Visual Studio\2022\BuildTools\VC\Auxiliary\Build\vcvars64.bat" set "VCVARS=%ProgramFiles%\Microsoft Visual Studio\2022\BuildTools\VC\Auxiliary\Build\vcvars64.bat"
if not defined VCVARS if exist "%ProgramFiles%\Microsoft Visual Studio\2022\Community\VC\Auxiliary\Build\vcvars64.bat" set "VCVARS=%ProgramFiles%\Microsoft Visual Studio\2022\Community\VC\Auxiliary\Build\vcvars64.bat"
if not defined VCVARS if exist "%ProgramFiles%\Microsoft Visual Studio\2022\Professional\VC\Auxiliary\Build\vcvars64.bat" set "VCVARS=%ProgramFiles%\Microsoft Visual Studio\2022\Professional\VC\Auxiliary\Build\vcvars64.bat"
if not defined VCVARS if exist "%ProgramFiles%\Microsoft Visual Studio\2022\Enterprise\VC\Auxiliary\Build\vcvars64.bat" set "VCVARS=%ProgramFiles%\Microsoft Visual Studio\2022\Enterprise\VC\Auxiliary\Build\vcvars64.bat"
if not defined VCVARS if exist "%ProgramFiles(x86)%\Microsoft Visual Studio\18\BuildTools\VC\Auxiliary\Build\vcvars64.bat" set "VCVARS=%ProgramFiles(x86)%\Microsoft Visual Studio\18\BuildTools\VC\Auxiliary\Build\vcvars64.bat"
if not defined VCVARS if exist "%ProgramFiles(x86)%\Microsoft Visual Studio\2019\BuildTools\VC\Auxiliary\Build\vcvars64.bat" set "VCVARS=%ProgramFiles(x86)%\Microsoft Visual Studio\2019\BuildTools\VC\Auxiliary\Build\vcvars64.bat"
if not defined VCVARS if exist "%ProgramFiles(x86)%\Microsoft Visual Studio\2019\Community\VC\Auxiliary\Build\vcvars64.bat" set "VCVARS=%ProgramFiles(x86)%\Microsoft Visual Studio\2019\Community\VC\Auxiliary\Build\vcvars64.bat"

if not defined VCVARS goto novc

echo Using: %VCVARS%
call "%VCVARS%" >nul 2>&1
if errorlevel 1 goto novc

cd src

echo.
echo [1/4] regenerating the app icon from SVG ^(optional, skipped if Python/Pillow is missing^)
python "..\tools\svg2ico.py" "..\assets\svg\app-icon.svg" "app.ico" >nul 2>&1
if errorlevel 1 echo       ^(skipped - using the committed app.ico^)

echo [2/4] compiling resources ^(embeds the admin manifest and the app icon^)
rc /nologo RapooBattery.rc
if errorlevel 1 goto fail

echo [3/4] compiling source
cl /nologo /W3 /O2 /EHsc /std:c++17 /utf-8 RapooBattery.cpp RapooBattery.res /Fe:RapooBattery.exe /link setupapi.lib hid.lib user32.lib gdi32.lib shell32.lib advapi32.lib
if errorlevel 1 goto fail

echo [4/4] collecting output
copy /y RapooBattery.exe ..\RapooBattery.exe >nul
del /q *.obj *.res RapooBattery.exe 2>nul

echo.
echo Build OK: %~dp0RapooBattery.exe
exit /b 0

:novc
echo [ERROR] vcvars64.bat not found or failed to run.
echo         Install the "Desktop development with C++" workload in Visual Studio,
echo         or run this script from a Developer Command Prompt.
exit /b 1

:fail
echo.
echo Build FAILED.
exit /b 1
