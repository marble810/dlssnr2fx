@echo off
setlocal EnableExtensions
set "VCVARS=C:\Program Files\Microsoft Visual Studio\2022\Community\VC\Auxiliary\Build\vcvars64.bat"
if not exist "%VCVARS%" (
  echo ERROR: vcvars64.bat not found:
  echo   %VCVARS%
  exit /b 1
)
call "%VCVARS%" >nul
where cl >nul 2>&1
if errorlevel 1 (
  echo ERROR: cl.exe not on PATH after vcvars64
  exit /b 1
)

set "CFLAGS=/nologo /O2 /std:c++17 /EHsc /MD /W3 /DNOMINMAX /DWIN32_LEAN_AND_MEAN /I third_party\ofx\include /I engine"
set "BUNDLE=dist\dlssnr.ofx.bundle\Contents\Win64"
set "SRCS=plugin\dlssnr_ofx.cpp plugin\describe.cpp plugin\render.cpp plugin\engine_client.cpp plugin\log.cpp plugin\pixio.cpp plugin\runtime.cpp"
set "ENGINE_SRCS=engine\session.cpp engine\gpu.cpp engine\ngx.cpp engine\composite.cpp engine\color_adapt.cpp engine\mvec.cpp engine\nvof.cpp engine\dav2.cpp"

if "%~1"=="" goto plugin
if /I "%~1"=="engine" goto engine
if /I "%~1"=="forwarder" goto forwarder
if /I "%~1"=="plugin" goto plugin
if /I "%~1"=="aex" goto aex
if /I "%~1"=="selftest" goto selftest
if /I "%~1"=="help" goto help
echo Unknown target %~1
goto help

:help
echo Usage: build.bat [engine ^| forwarder ^| plugin ^| aex ^| selftest]
echo   engine     build dist\engine\dlssnr2fx.dll ^(also builds the forwarder^)
echo   forwarder  build dist\engine\nvngx.dll_dlssnr.dll and copy into runtime\
echo   plugin     build OFX bundle and copy the engine beside the plug-in
echo   aex        build After Effects plug-in and copy the engine into dist\aex
echo   selftest   build/run the in-process engine and pixel conversion tests
echo.
echo Pack a versioned zip with release.bat (includes fetch-runtime.bat).
exit /b 0

:engine
mkdir dist\engine 2>nul
cl %CFLAGS% /D DLSSNR2FX_EXPORTS %ENGINE_SRCS% /LD /Fe:dist\engine\dlssnr2fx.dll /link /DEF:engine\dlssnr2fx.def d3d12.lib d3d11.lib dxgi.lib ole32.lib
if errorlevel 1 exit /b 1
del /q *.obj dist\engine\dlssnr2fx.lib dist\engine\dlssnr2fx.exp 2>nul
echo Built dist\engine\dlssnr2fx.dll
dumpbin /exports dist\engine\dlssnr2fx.dll | findstr /C:"nr_eval" /C:"nr_cancel" /C:"nr_shutdown"
if errorlevel 1 exit /b 1
cl %CFLAGS% /D DLSSNR2FX_EXPORTS engine\forwarder.cpp /LD /Fe:dist\engine\nvngx.dll_dlssnr.dll /link kernel32.lib
if errorlevel 1 exit /b 1
mkdir runtime 2>nul
copy /Y dist\engine\nvngx.dll_dlssnr.dll runtime\nvngx.dll_dlssnr.dll >nul
if errorlevel 1 echo WARNING: runtime\nvngx.dll_dlssnr.dll is locked; dist\engine copy is current
del /q *.obj dist\engine\nvngx.dll_dlssnr.lib dist\engine\nvngx.dll_dlssnr.exp 2>nul
echo Built dist\engine\nvngx.dll_dlssnr.dll
dumpbin /exports dist\engine\nvngx.dll_dlssnr.dll | findstr /C:"fwd_create" /C:"fwd_evaluate" /C:"fwd_release" /C:"fwd_set_slots"
exit /b 0

:forwarder
mkdir dist\engine 2>nul
cl %CFLAGS% /D DLSSNR2FX_EXPORTS engine\forwarder.cpp /LD /Fe:dist\engine\nvngx.dll_dlssnr.dll /link kernel32.lib
if errorlevel 1 exit /b 1
mkdir runtime 2>nul
copy /Y dist\engine\nvngx.dll_dlssnr.dll runtime\nvngx.dll_dlssnr.dll >nul
if errorlevel 1 echo WARNING: runtime\nvngx.dll_dlssnr.dll is locked; dist\engine copy is current
del /q *.obj dist\engine\nvngx.dll_dlssnr.lib dist\engine\nvngx.dll_dlssnr.exp 2>nul
echo Built dist\engine\nvngx.dll_dlssnr.dll, copied to runtime\nvngx.dll_dlssnr.dll
dumpbin /exports dist\engine\nvngx.dll_dlssnr.dll | findstr /C:"fwd_create" /C:"fwd_evaluate" /C:"fwd_release" /C:"fwd_set_slots"
exit /b %ERRORLEVEL%

:plugin
call "%~f0" engine
if errorlevel 1 exit /b 1
if not exist plugin\dlssnr_ofx.cpp (
  echo ERROR: plugin sources missing
  exit /b 1
)
if not exist third_party\ofx\include\ofxCore.h (
  echo ERROR: OFX headers missing under third_party\ofx\include
  exit /b 1
)
mkdir "%BUNDLE%" 2>nul
copy /Y plugin\Info.plist dist\dlssnr.ofx.bundle\Contents\Info.plist >nul
cl %CFLAGS% %SRCS% /LD /Fe:dist\dlssnr_build.ofx /link /DEF:plugin\dlssnr_ofx.def ole32.lib user32.lib
if errorlevel 1 exit /b 1
copy /Y dist\dlssnr_build.ofx "%BUNDLE%\dlssnr.ofx" >nul
if errorlevel 1 (
  echo WARNING: %BUNDLE%\dlssnr.ofx is locked ^(close Natron^). New binary is dist\dlssnr_build.ofx
  exit /b 2
)
copy /Y dist\engine\dlssnr2fx.dll "%BUNDLE%\dlssnr2fx.dll" >nul
if errorlevel 1 exit /b 1
del /q *.obj dist\dlssnr_build.ofx dist\dlssnr_build.lib dist\dlssnr_build.exp 2>nul
if exist runtime\nvngx_dlssnr.dll if not exist "%BUNDLE%\runtime\nvngx_dlssnr.dll" (
  mklink /J "%BUNDLE%\runtime" "%CD%\runtime" >nul 2>&1
  if exist "%BUNDLE%\runtime\nvngx_dlssnr.dll" echo linked %BUNDLE%\runtime -^> runtime\
)
echo Built %BUNDLE%\dlssnr.ofx and dlssnr2fx.dll
dumpbin /exports "%BUNDLE%\dlssnr.ofx" | findstr OfxGet
exit /b %ERRORLEVEL%

:aex
call "%~f0" engine
if errorlevel 1 exit /b 1
if not exist plugin\aex\dlssnr_aex.cpp (
  echo ERROR: plugin\aex\dlssnr_aex.cpp missing
  exit /b 1
)
if "%AE_SDK_DIR%"=="" set "AE_SDK_DIR=AfterEffectsSDK_25.6_61_win\ae25.6_61.64bit.AfterEffectsSDK\Examples"
if not exist "%AE_SDK_DIR%\Headers\AE_Effect.h" (
  echo ERROR: AE SDK Headers not found under %AE_SDK_DIR%
  echo   Override with AE_SDK_DIR, e.g. set AE_SDK_DIR=C:\aesdk\Examples
  exit /b 1
)
if not exist "%AE_SDK_DIR%\Resources\PiPLtool.exe" (
  echo ERROR: PiPLtool.exe not found under %AE_SDK_DIR%\Resources
  exit /b 1
)
mkdir build\aex 2>nul
mkdir dist\aex 2>nul
set "AEX_CFLAGS=/nologo /O2 /std:c++17 /EHsc /MD /W3 /D_WINDOWS /DNOMINMAX /DWIN32_LEAN_AND_MEAN /I "%AE_SDK_DIR%\Headers" /I "%AE_SDK_DIR%\Headers\SP" /I "%AE_SDK_DIR%\Headers\Win" /I "%AE_SDK_DIR%\Util" /I plugin /I engine"

echo [1/5] Preprocess PiPL .r
cl %AEX_CFLAGS% /EP plugin\aex\dlssnr_aexPiPL.r > build\aex\dlssnr_aexPiPL.rr
if errorlevel 1 exit /b 1
echo [2/5] PiPLtool
"%AE_SDK_DIR%\Resources\PiPLtool.exe" build\aex\dlssnr_aexPiPL.rr build\aex\dlssnr_aexPiPL.rrc
if errorlevel 1 exit /b 1
echo [3/5] Preprocess .rrc to .rc
cl /nologo /D MSWindows /EP build\aex\dlssnr_aexPiPL.rrc > build\aex\dlssnr_aexPiPL.rc
if errorlevel 1 exit /b 1
echo [4/5] rc.exe compile resource
rc /nologo /fo build\aex\dlssnr_aexPiPL.res build\aex\dlssnr_aexPiPL.rc
if errorlevel 1 exit /b 1
echo [5/5] Compile + link plug-in
cl %AEX_CFLAGS% plugin\aex\dlssnr_aex.cpp plugin\engine_client.cpp plugin\log.cpp plugin\pixio.cpp plugin\runtime.cpp build\aex\dlssnr_aexPiPL.res /LD /Fe:build\aex\dlssnr_build.aex /link ole32.lib user32.lib
if errorlevel 1 exit /b 1
copy /Y build\aex\dlssnr_build.aex dist\aex\dlssnr.aex >nul
if errorlevel 1 (
  echo WARNING: dist\aex\dlssnr.aex is locked ^(close After Effects^). New binary is build\aex\dlssnr_build.aex
  del /q *.obj build\aex\dlssnr_build.lib build\aex\dlssnr_build.exp 2>nul
  exit /b 2
)
copy /Y dist\engine\dlssnr2fx.dll dist\aex\dlssnr2fx.dll >nul
if errorlevel 1 exit /b 1
if exist runtime\nvngx_dlssnr.dll if not exist dist\aex\runtime\nvngx_dlssnr.dll (
  mklink /J dist\aex\runtime "%CD%\runtime" >nul 2>&1
  if exist dist\aex\runtime\nvngx_dlssnr.dll echo linked dist\aex\runtime -^> runtime\
)
del /q *.obj build\aex\dlssnr_build.aex build\aex\dlssnr_build.lib build\aex\dlssnr_build.exp 2>nul
echo Built dist\aex\dlssnr.aex and dlssnr2fx.dll
dumpbin /exports dist\aex\dlssnr.aex | findstr EffectMain
exit /b %ERRORLEVEL%

:selftest
call "%~f0" engine
if errorlevel 1 exit /b 1
cl /nologo /W3 /TC /I engine /c test\engine_header_c.c /Fo:test\engine_header_c.obj
if errorlevel 1 exit /b 1
cl %CFLAGS% test\engine_selftest.cpp engine\composite.cpp engine\color_adapt.cpp engine\mvec.cpp /Fe:test\engine_selftest.exe
if errorlevel 1 exit /b 1
cl %CFLAGS% test\pixio_selftest.cpp plugin\pixio.cpp /Fe:test\pixio_selftest.exe
if errorlevel 1 exit /b 1
cl %CFLAGS% test\runtime_selftest.cpp plugin\runtime.cpp plugin\log.cpp /Fe:test\runtime_selftest.exe /link ole32.lib user32.lib
if errorlevel 1 exit /b 1
cl %CFLAGS% test\engine_client_selftest.cpp plugin\engine_client.cpp plugin\log.cpp /Fe:test\engine_client_selftest.exe /link ole32.lib user32.lib
if errorlevel 1 exit /b 1
cl %CFLAGS% test\gpu_selftest.cpp engine\gpu.cpp /Fe:test\gpu_selftest.exe /link d3d12.lib dxgi.lib ole32.lib
if errorlevel 1 exit /b 1
del /q *.obj test\engine_header_c.obj 2>nul
test\engine_selftest.exe
if errorlevel 1 exit /b %ERRORLEVEL%
test\pixio_selftest.exe
if errorlevel 1 exit /b %ERRORLEVEL%
test\runtime_selftest.exe
if errorlevel 1 exit /b %ERRORLEVEL%
test\engine_client_selftest.exe
if errorlevel 1 exit /b %ERRORLEVEL%
test\gpu_selftest.exe
exit /b %ERRORLEVEL%
