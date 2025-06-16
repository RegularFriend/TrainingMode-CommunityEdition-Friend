@echo off
setlocal

REM set directory to location of this file. Needed when drag n dropping across directories.
cd /d %~dp0

if exist "C:/devkitPro/devkitPPC" (
    echo ERROR: devkitPro not found at "C:/devkitPro/devkitPPC"
    echo Please install devkitPro with the GameCube package
    goto end
) else (
    echo found devkitPro
    set "PATH=%PATH%;C:\devkitPro\devkitPPC\bin"
)

C:\devkitPro\msys2\msys2_shell.cmd -msys2 -here
exit 1

:end

REM pause if not run from command line
echo %CMDCMDLINE% | findstr /C:"/c">nul && pause
