@echo off
setlocal EnableExtensions
title kurasagi build

rem ============================================================
rem  kurasagi one-click build
rem  Usage  : build.bat [Debug^|Release] [x64^|ARM64] [nopause]
rem  Default: Release x64
rem  Needs  : Visual Studio 2022 + WDK driver workload
rem ============================================================

set "NOPAUSE="
if /i "%~3"=="nopause" set "NOPAUSE=1"

rem Run the real work in a subroutine so that even an early failure
rem still comes back here and keeps the window open.
call :main %*
set "RC=%ERRORLEVEL%"

echo.
if "%RC%"=="0" (
    echo === BUILD SUCCEEDED ===
) else (
    echo === BUILD FAILED ^(exit code %RC%^) ===
    echo     Log file: "%ROOT%build.log"
)
echo.
if not defined NOPAUSE pause
endlocal & exit /b %RC%


:main
set "ROOT=%~dp0"
set "SOLUTION=%ROOT%kurasagi.sln"
set "BUILD_LOG=%ROOT%build.log"
set "DIAG_LOG=%ROOT%build.diag.txt"

set "CONFIG=%~1"
if "%CONFIG%"=="" set "CONFIG=Release"
set "PLATFORM=%~2"
if "%PLATFORM%"=="" set "PLATFORM=x64"

if /i "%CONFIG%"=="Debug" goto :config_ok
if /i "%CONFIG%"=="Release" goto :config_ok
echo [ERROR] Unknown configuration "%CONFIG%" - use Debug or Release.
exit /b 1
:config_ok

if /i "%PLATFORM%"=="x64" goto :platform_ok
if /i "%PLATFORM%"=="ARM64" goto :platform_ok
echo [ERROR] Unknown platform "%PLATFORM%" - use x64 or ARM64.
exit /b 1
:platform_ok

echo.
echo ====================================================
echo   kurasagi build  ^|  %CONFIG%  ^|  %PLATFORM%
echo ====================================================
echo.

if not exist "%SOLUTION%" (
    echo [ERROR] Solution not found: "%SOLUTION%"
    echo         Please keep build.bat in the same folder as kurasagi.sln.
    exit /b 1
)

echo [1/3] Locating Visual Studio / MSBuild ...
set "VSWHERE=%ProgramFiles(x86)%\Microsoft Visual Studio\Installer\vswhere.exe"
set "MSBUILD="
set "MSBUILD_FALLBACK="
set "DRIVER_TOOLSET="
if not exist "%VSWHERE%" goto :vs_done
for /f "usebackq delims=" %%i in (`"%VSWHERE%" -all -products * -requires Microsoft.Component.MSBuild -find MSBuild\**\Bin\MSBuild.exe`) do (
    if not defined MSBUILD call :probe_msbuild "%%i"
)
:vs_done
if not defined MSBUILD if defined MSBUILD_FALLBACK set "MSBUILD=%MSBUILD_FALLBACK%"

if not defined MSBUILD (
    echo [ERROR] MSBuild not found.
    echo         Install Visual Studio 2022 and select the
    echo         "Desktop development with C++" workload.
    exit /b 1
)
echo         %MSBUILD%
if not defined DRIVER_TOOLSET (
    echo [WARN]  WindowsKernelModeDriver10.0 toolset not found for this
    echo         Visual Studio instance - the WDK integration is missing.
    echo         Install the "Windows Driver Kit" component for THIS instance
    echo         in Visual Studio Installer ^(or install full VS 2022 + WDK^).
)

echo [2/3] Checking Windows Driver Kit ...
set "WDK_ROOT=%ProgramFiles(x86)%\Windows Kits\10"
set "WDKContentRoot=%WDK_ROOT%\"

rem Pick the newest SDK target that is COMPLETE: kernel-mode headers
rem (ntddk.h), WindowsDriver targets and the DriverKit MSBuild task DLL.
rem A half-installed WDK (targets present, task DLL missing) must be skipped.
set "WDK_VER="
set "WDK_INCOMPLETE="
for /f "usebackq delims=" %%i in (`dir /b /ad /o-n "%WDK_ROOT%\Include\10.*" 2^>nul`) do (
    if not defined WDK_VER call :check_wdk "%%i"
)

if not defined WDK_VER (
    echo [ERROR] No complete WDK installation found under "%WDK_ROOT%".
    if defined WDK_INCOMPLETE (
        echo         WDK %WDK_INCOMPLETE% exists but its build task DLL is missing:
        echo         build\%WDK_INCOMPLETE%\bin\Microsoft.DriverKit.Build.Tasks.*.dll
        echo         The WDK installation is incomplete or damaged.
        echo         Run the WDK installer and choose "Repair", or reinstall
        echo         the "Windows Driver Kit" component in Visual Studio Installer.
    ) else (
        echo         Install the "Windows Driver Kit" component via Visual Studio
        echo         Installer ^(or run the WDK installer^).
    )
    exit /b 1
)
echo         WDK headers: %WDK_VER%

echo [3/3] Building ...
echo.
if exist "%BUILD_LOG%" del /f /q "%BUILD_LOG%" >nul 2>&1
"%MSBUILD%" "%SOLUTION%" -nologo -m -t:Build -p:Configuration=%CONFIG% -p:Platform=%PLATFORM% -p:WindowsTargetPlatformVersion=%WDK_VER% -p:WindowsSDKVersion=%WDK_VER%\ -v:minimal -fl "-flp:LogFile=%BUILD_LOG%;Verbosity=normal"
if errorlevel 1 goto :build_failed

echo.
echo Build finished.
set "OUTDIR=%ROOT%%PLATFORM%\%CONFIG%"
if exist "%OUTDIR%" (
    echo Output: %OUTDIR%
    if exist "%OUTDIR%\kurasagi.sys" echo   - kurasagi.sys
    if exist "%OUTDIR%\VerifyPg.sys" echo   - VerifyPg.sys
    if exist "%OUTDIR%\kurasagi\kurasagi.inf" echo   - package: %OUTDIR%\kurasagi\
    if exist "%OUTDIR%\VerifyPg\VerifyPg.inf" echo   - package: %OUTDIR%\VerifyPg\
)
exit /b 0


:build_failed
echo.
echo [ERROR] Build failed. MSBuild log: "%BUILD_LOG%"
echo [INFO] Writing environment diagnostics to "%DIAG_LOG%" ...
for %%I in ("%MSBUILD%") do set "MSBUILD_DIR=%%~dpI"
(
    echo === kurasagi build diagnostics ===
    echo WDK_VER=%WDK_VER%
    echo WDK_ROOT=%WDK_ROOT%
    echo WDKContentRoot=%WDKContentRoot%
    echo MSBUILD=%MSBUILD%
    echo DRIVER_TOOLSET=%DRIVER_TOOLSET%
    echo CONFIG=%CONFIG%  PLATFORM=%PLATFORM%
    echo.
    echo --- Windows Kits Include directory ---
    dir /b "%WDK_ROOT%\Include" 2^>^&1
    echo.
    echo --- Selected kernel include folder ---
    dir /b "%WDK_ROOT%\Include\%WDK_VER%\km" 2^>^&1
    echo.
    echo --- WDK build props directories ---
    dir /b "%WDK_ROOT%\build" 2^>^&1
    echo.
    echo --- WindowsDriver props files ---
    dir /b /s "%WDK_ROOT%\build\WindowsDriver*.props" 2^>^&1
    echo.
    echo --- PlatformToolsets ---
    dir /b "%MSBUILD_DIR%..\..\Microsoft\VC\v170\Platforms\x64\PlatformToolsets" 2^>^&1
    echo.
    echo --- Resolved project properties ---
    "%MSBUILD%" "%ROOT%kurasagi\kurasagi.vcxproj" -nologo -p:Configuration=%CONFIG% -p:Platform=%PLATFORM% -p:WindowsTargetPlatformVersion=%WDK_VER% -p:WindowsSDKVersion=%WDK_VER%\ -getProperty:IncludePath -getProperty:WindowsTargetPlatformVersion -getProperty:WindowsSDKVersion -getProperty:WDKContentRoot -getProperty:PlatformToolset 2^>^&1
) > "%DIAG_LOG%" 2>&1
echo [INFO] Diagnostics written. Please send "%DIAG_LOG%" for analysis.
exit /b 1


rem Pick the MSBuild whose Visual Studio instance has the WDK driver toolset.
:probe_msbuild
set "MB_PATH=%~1"
for %%I in ("%MB_PATH%") do set "MB_DIR=%%~dpI"
if exist "%MB_DIR%..\..\Microsoft\VC\v170\Platforms\x64\PlatformToolsets\WindowsKernelModeDriver10.0" goto :probe_hit
if exist "%MB_DIR%..\..\..\Common7\IDE\VC\VCTargets\Platforms\x64\PlatformToolsets\WindowsKernelModeDriver10.0" goto :probe_hit
if not defined MSBUILD_FALLBACK set "MSBUILD_FALLBACK=%MB_PATH%"
exit /b 1

:probe_hit
set "MSBUILD=%MB_PATH%"
set "DRIVER_TOOLSET=1"
exit /b 0


rem A usable WDK target needs headers, WindowsDriver targets and the task DLL.
:check_wdk
set "CAND_VER=%~1"
if not exist "%WDK_ROOT%\Include\%CAND_VER%\km\ntddk.h" exit /b 1
if not exist "%WDK_ROOT%\build\%CAND_VER%\WindowsDriver.common.targets" exit /b 1
if not exist "%WDK_ROOT%\build\%CAND_VER%\bin\Microsoft.DriverKit.Build.Tasks.*.dll" (
    if not defined WDK_INCOMPLETE set "WDK_INCOMPLETE=%CAND_VER%"
    exit /b 1
)
set "WDK_VER=%CAND_VER%"
exit /b 0
