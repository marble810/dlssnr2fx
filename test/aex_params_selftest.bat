@echo off
setlocal EnableExtensions
cd /d "%~dp0.."
if "%AE_SDK_DIR%"=="" set "AE_SDK_DIR=AfterEffectsSDK_25.6_61_win\ae25.6_61.64bit.AfterEffectsSDK\Examples"
if not exist "%AE_SDK_DIR%\Headers\AE_Effect.h" (
  echo ERROR: set AE_SDK_DIR to the AE SDK Examples directory.
  exit /b 1
)
call "C:\Program Files\Microsoft Visual Studio\2022\Community\VC\Auxiliary\Build\vcvars64.bat" >nul
if errorlevel 1 exit /b 1
mkdir build\aex-test 2>nul
cl /nologo /std:c++17 /EHsc /MD /W3 /D_WINDOWS /DNOMINMAX /DWIN32_LEAN_AND_MEAN /I "%AE_SDK_DIR%\Headers" /I "%AE_SDK_DIR%\Headers\SP" /I "%AE_SDK_DIR%\Headers\Win" /I "%AE_SDK_DIR%\Util" test\aex_params_selftest.cpp plugin\pixio.cpp /Fo:build\aex-test\ /Fe:build\aex-test\aex_params_selftest.exe
if errorlevel 1 exit /b 1
build\aex-test\aex_params_selftest.exe
exit /b %ERRORLEVEL%
