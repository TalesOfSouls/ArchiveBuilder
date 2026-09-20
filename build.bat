@echo off
setlocal enabledelayedexpansion

set "EXE_NAME=archive_builder"
set "DESTINATION_DIR=..\build\archive_builder"

IF NOT EXIST ..\build mkdir ..\build
IF NOT EXIST "%DESTINATION_DIR%" mkdir "%DESTINATION_DIR%"

if not defined DevEnvDir (call "C:\Program Files\Microsoft Visual Studio\2022\Community\VC\Auxiliary\Build\vcvars64.bat")

if "%Platform%" neq "x64" (
    echo ERROR: Platform is not "x64" - previous bat call failed.
    exit /b 1
)

cd "%DESTINATION_DIR%"
del *.pdb /F /Q > NUL 2> NUL
del *.idb /F /Q > NUL 2> NUL
cd "..\..\ArchiveBuilder"

REM Use /showIncludes for include debugging

set "BUILD_TYPE=DEBUG"
set "BUILD_FLAGS=/Od /Z7 /WX /FC /RTC1 /DDEBUG"

REM Use /showIncludes for include debugging
REM Use /VERBOSE:LIB for the linker to show all libs
set "DEFAULT_COMPILER_ARGS=/Oi /MT /nologo /Gm- /GR- /EHsc /W4 /wd4201 /wd4706 /wd4324 /Zc:wchar_t /Zc:forScope /Zc:inline /permissive- /std:c++20 /fp:fast /D CPP_VERSION=20 /D _WIN32 /D _WINDOWS /D _UNICODE /D UNICODE /D _CRT_SECURE_NO_WARNINGS /D __SSE4_2__"

set "DEBUG_DATA=/Fd"%DESTINATION_DIR%\%EXE_NAME%.pdb" /Fm"%DESTINATION_DIR%\%EXE_NAME%.map""

REM Parse command-line arguments
if "%1"=="-r" (
    set "BUILD_TYPE=RELEASE"
    set "BUILD_FLAGS=/O2 /D NDEBUG"

    set "DEBUG_DATA="
)
if "%1"=="-d" (
    set "BUILD_TYPE=DEBUG"
    set "BUILD_FLAGS=/Od /Z7 /WX /FC /RTC1 /DDEBUG"

    set "DEBUG_DATA=/Fd"%DESTINATION_DIR%\%EXE_NAME%.pdb" /Fm"%DESTINATION_DIR%\%EXE_NAME%.map""
)

REM Create main program
cl ^
    %BUILD_FLAGS% %DEFAULT_COMPILER_ARGS% ^
    /Fo"%DESTINATION_DIR%/" /Fe"%DESTINATION_DIR%/%EXE_NAME%.exe" %DEBUG_DATA% ^
    "%EXE_NAME%.cpp" ^
    /link /INCREMENTAL:no ^
    /SUBSYSTEM:CONSOLE /MACHINE:X64 ^
    kernel32.lib user32.lib gdi32.lib winmm.lib

if errorlevel 1 (
    echo ERROR: %EXE_NAME% compile/link failed
    endlocal
    exit /b 1
)

endlocal
exit /b 0