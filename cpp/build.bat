@echo off
rem 金橙电竞-显示器控制 —— C++ 单文件构建（完整链接库清单 + 版本注入）
rem 用法: build.bat [版本号]   （默认 1.6.3；需 MSVC x64 命令行环境或手工设 INCLUDE/LIB/PATH）
setlocal enabledelayedexpansion
if "%~1"=="" (set APPVER=1.6.3) else (set APPVER=%~1)
rem 从版本号拆数字部分，供 app.rc 的 FILEVERSION 使用（1.6.3 -> 1,5,5）
for /f "tokens=1,2,3 delims=." %%a in ("%APPVER%") do (
    set VER1=%%a
    set VER2=%%b
    set VER3=%%c
)
if "%VER2%"=="" set VER2=0
if "%VER3%"=="" set VER3=0
set VCDIR=D:/Program Files (x86)/Microsoft Visual Studio/2022/BuildTools/VC/Tools/MSVC/14.44.35207
set SDKDIR=C:/Program Files (x86)/Windows Kits/10
set "INCLUDE=%VCDIR%\include;%SDKDIR%\Include\10.0.26100.0\ucrt;%SDKDIR%\Include\10.0.26100.0\shared;%SDKDIR%\Include\10.0.26100.0\um;%SDKDIR%\Include\10.0.26100.0\winrt"
set "LIB=%VCDIR%\lib\x64;%SDKDIR%\Lib\10.0.26100.0\ucrt\x64;%SDKDIR%\Lib\10.0.26100.0\um\x64"
set "PATH=%VCDIR%\bin\Hostx64\x64;%PATH%"
rem -c 65001：app.rc 是 UTF-8（含中文公司名），不指定代码页 rc.exe 会按 ANSI 解析失败
rem 每次重编资源：版本号变了必须重新生成，否则 exe 属性里还是旧版本
"%SDKDIR%\bin\10.0.26100.0\x64\rc.exe" /nologo /c 65001 /d APP_VERSION=\"%APPVER%\" ^
   /d APP_VER_MAJ=%VER1% /d APP_VER_MIN=%VER2% /d APP_VER_PAT=%VER3% ^
   /fo app.res app.rc
cl /nologo /std:c++20 /O2 /MT /EHsc /utf-8 /guard:cf /DAPP_VERSION=\"%APPVER%\" main.cpp app.res ^
   /Fe:"金橙电竞-显示器控制.exe" /link /guard:cf /DYNAMICBASE /NXCOMPAT /HIGHENTROPYVA /OPT:REF /OPT:ICF /SUBSYSTEM:WINDOWS ^
   setupapi.lib cfgmgr32.lib ole32.lib oleaut32.lib wbemuuid.lib winhttp.lib ^
   user32.lib gdi32.lib d2d1.lib dwrite.lib windowscodecs.lib shell32.lib advapi32.lib dxva2.lib dwmapi.lib
if errorlevel 1 (echo. & echo 编译失败 & exit /b 1)
echo.
echo 完成: 金橙电竞-显示器控制.exe（版本 %APPVER%，拷到客户机即可用）
endlocal
