@echo off
setlocal EnableExtensions
chcp 65001 >nul

rem ============================================================
rem  kurasagi 一键编译脚本
rem  用法: build.bat [Debug^|Release] [x64^|ARM64]
rem  默认: build.bat  ->  Release x64
rem  依赖: Visual Studio 2022 + "Windows 驱动程序开发" 工作负载(WDK)
rem ============================================================

set "ROOT=%~dp0"
set "SOLUTION=%ROOT%kurasagi.sln"

set "CONFIG=%~1"
if "%CONFIG%"=="" set "CONFIG=Release"
set "PLATFORM=%~2"
if "%PLATFORM%"=="" set "PLATFORM=x64"

if /i "%CONFIG%"=="Debug" goto :config_ok
if /i "%CONFIG%"=="Release" goto :config_ok
echo [错误] 未知配置: "%CONFIG%"，只支持 Debug 或 Release。
goto :fail
:config_ok

if /i "%PLATFORM%"=="x64" goto :platform_ok
if /i "%PLATFORM%"=="ARM64" goto :platform_ok
echo [错误] 未知平台: "%PLATFORM%"，只支持 x64 或 ARM64。
goto :fail
:platform_ok

echo.
echo ====================================================
echo   kurasagi 编译  ^|  %CONFIG%  ^|  %PLATFORM%
echo ====================================================
echo.

if not exist "%SOLUTION%" (
    echo [错误] 找不到解决方案文件: "%SOLUTION%"
    goto :fail
)

rem ---------- 定位 Visual Studio 的 MSBuild ----------
set "VSWHERE=%ProgramFiles(x86)%\Microsoft Visual Studio\Installer\vswhere.exe"
set "MSBUILD="

if not exist "%VSWHERE%" goto :find_msbuild_done
for /f "usebackq delims=" %%i in (`"%VSWHERE%" -latest -products * -requires Microsoft.Component.MSBuild -find MSBuild\**\Bin\MSBuild.exe`) do set "MSBUILD=%%i"
:find_msbuild_done

if not defined MSBUILD (
    echo [错误] 未找到 Visual Studio / MSBuild。
    echo        请安装 Visual Studio 2022，并勾选 "使用 C++ 的桌面开发" 工作负载。
    goto :fail
)
echo [信息] MSBuild: %MSBUILD%

rem ---------- 检查 WDK 是否安装 ----------
set "WDK_OK="
for /d %%i in ("%ProgramFiles(x86)%\Windows Kits\10\Include\*") do if exist "%%i\km" set "WDK_OK=1"

if not defined WDK_OK (
    echo [错误] 未检测到 Windows Driver Kit (WDK)。
    echo        请在 Visual Studio Installer 中安装 "Windows 驱动程序开发" 工作负载
    echo        ^(会自动安装 WDK 与 Windows SDK^)。
    goto :fail
)
echo [信息] WDK: 已检测到
echo.
echo [信息] 开始编译，请稍候...
echo.

rem ---------- 调用 MSBuild 编译整个解决方案 ----------
"%MSBUILD%" "%SOLUTION%" -nologo -m -t:Build -p:Configuration=%CONFIG% -p:Platform=%PLATFORM% -v:minimal
if errorlevel 1 goto :fail

echo.
echo [成功] 编译完成。

set "OUTDIR=%ROOT%%PLATFORM%\%CONFIG%"
if exist "%OUTDIR%" (
    echo        输出目录: %OUTDIR%
    if exist "%OUTDIR%\kurasagi.sys" echo          - kurasagi.sys
    if exist "%OUTDIR%\VerifyPg.sys" echo          - VerifyPg.sys
    if exist "%OUTDIR%\kurasagi\kurasagi.inf" echo          - 驱动包: %OUTDIR%\kurasagi\
    if exist "%OUTDIR%\VerifyPg\VerifyPg.inf" echo          - 驱动包: %OUTDIR%\VerifyPg\
)
goto :done

:fail
echo.
echo [失败] 编译未通过，请查看上方 MSBuild 输出的错误信息。
set "FAILED=1"

:done
echo.
rem 双击运行时保留窗口，方便查看结果
echo(%cmdcmdline% | find /i "%~nx0" >nul
if not errorlevel 1 pause
if defined FAILED (endlocal & exit /b 1)
endlocal
