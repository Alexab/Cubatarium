@echo off
setlocal EnableExtensions
cd /d "%~dp0"

set "SRC_BIN=..\bin"
set "OUT=InstallSourcesQt"
set "OUT_BIN=%OUT%\bin"

if not exist "%SRC_BIN%\Cubatarium.exe" (
    echo ERROR: Release/Debug build not found: %SRC_BIN%\Cubatarium.exe
    echo Build the project first ^(cmake --build ...^), then run this script.
    exit /b 1
)

if not exist "%OUT%" mkdir "%OUT%"
if not exist "%OUT_BIN%" mkdir "%OUT_BIN%"

echo Staging installer files into %OUT% ...

rem Executable and runtime DLLs (whitelist only; no Qt / no *d debug copies)
xcopy "%SRC_BIN%\Cubatarium.exe" "%OUT_BIN%\" /Y /D
for %%F in (
    glfw3.dll
    glew32.dll
    freetype.dll
    zlib1.dll
    bz2.dll
    libpng16.dll
    brotlidec.dll
    brotlicommon.dll
) do (
    if exist "%SRC_BIN%\%%F" xcopy "%SRC_BIN%\%%F" "%OUT_BIN%\" /Y /D
)
rem Remove stale DLLs from a previous staging (e.g. Qt / Debug leftovers)
del /q "%OUT_BIN%\Qt5*.dll" 2>nul
del /q "%OUT_BIN%\*d.dll" 2>nul

rem Config next to exe (worlds/ and saves are created at runtime under bin)
if exist "%SRC_BIN%\config.json" (
    xcopy "%SRC_BIN%\config.json" "%OUT_BIN%\" /Y /D
) else if exist "..\config.json.example" (
    xcopy "..\config.json.example" "%OUT_BIN%\config.json" /Y /D
)

rem Game data: prefer CMake POST_BUILD output in bin/, fall back to repo root
call :CopyTree shaders
call :CopyTree prefabs
call :CopyTree models
call :CopyTree textures
call :CopyTree content

echo Done staging. Building setup with Actual Installer ...
"C:\Program Files (x86)\Actual Installer\actinst.exe" /S "CubatariumQt.aip"
exit /b %ERRORLEVEL%

:CopyTree
set "NAME=%~1"
if exist "%SRC_BIN%\%NAME%" (
    xcopy "%SRC_BIN%\%NAME%" "%OUT_BIN%\%NAME%\" /E /I /Y /D
) else if exist "..\%NAME%" (
    echo WARNING: %SRC_BIN%\%NAME% missing, copying from repo: ..\%NAME%
    xcopy "..\%NAME%" "%OUT_BIN%\%NAME%\" /E /I /Y /D
) else (
    echo WARNING: %NAME% not found in bin or repo root — skipped
)
exit /b 0
