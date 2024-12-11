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
del *.pdb > NUL 2> NUL
del *.idb > NUL 2> NUL
cd ..\..\ArchiveBuilder

REM Use /showIncludes for include debugging

set BUILD_TYPE=DEBUG
set BUILD_FLAGS=/Od /Z7 /WX /FC /RTC1 /DDEBUG

set "DEBUG_DATA=/Fd"%DESTINATION_DIR%\%EXE_NAME%.pdb" /Fm"%DESTINATION_DIR%\%EXE_NAME%.map""

REM Parse command-line arguments
if "%1"=="-r" (
    set BUILD_TYPE=RELEASE
    set BUILD_FLAGS=/O2 /D NDEBUG

    set DEBUG_DATA=
)
if "%1"=="-d" (
    set BUILD_TYPE=DEBUG
    set BUILD_FLAGS=/Od /Z7 /WX /FC /RTC1 /DDEBUG

    set "DEBUG_DATA=/Fd"%DESTINATION_DIR%\%EXE_NAME%.pdb" /Fm"%DESTINATION_DIR%\%EXE_NAME%.map""
)

REM Create main program
cl ^
    %BUILD_FLAGS% /MT /nologo /Gm- /GR- /EHsc /W4 /wd4201 /wd4706 ^
    /wd4018 /wd4389 /wd4100 /wd4244 /wd4189 ^
    /fp:precise /Zc:wchar_t /Zc:forScope /Zc:inline /std:c++20 ^
    /D WIN32 /D _WINDOWS /D _UNICODE /D UNICODE ^
    /D _CRT_SECURE_NO_WARNINGS ^
    /Fo"%DESTINATION_DIR%/" /Fe"%DESTINATION_DIR%/%EXE_NAME%.exe" %DEBUG_DATA% ^
    "%EXE_NAME%.cpp" ^
    /link /INCREMENTAL:no ^
    /SUBSYSTEM:CONSOLE /MACHINE:X64 ^
    kernel32.lib user32.lib gdi32.lib winmm.lib