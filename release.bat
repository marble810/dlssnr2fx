@echo off
setlocal EnableExtensions EnableDelayedExpansion
cd /d "%~dp0"

set "VER="
for /f "usebackq tokens=* delims=" %%V in ("VERSION") do (
  if not defined VER set "VER=%%V"
)
if not defined VER (
  echo ERROR: VERSION file missing or empty
  exit /b 1
)

if "%~1"=="" goto all
if /I "%~1"=="help" goto help
if /I "%~1"=="pack" goto pack_only
if /I "%~1"=="publish" goto publish
echo Unknown target %~1
goto help

:help
echo Usage: release.bat [pack ^| publish]
echo   no args   selftest + OFX + AEX, then pack dist\dlssnr2fx-%VER%-win64.zip
echo   pack      zip existing dist\ ^(no compile^)
echo   publish   build + pack, then gh release create v%VER%
echo.
echo Zip includes fetch-runtime.bat. It does not include nvngx_dlssnr.dll.
exit /b 0

:all
call :build
if errorlevel 1 exit /b 1
call :pack
exit /b %ERRORLEVEL%

:pack_only
call :pack
exit /b %ERRORLEVEL%

:publish
call :build
if errorlevel 1 exit /b 1
call :pack
if errorlevel 1 exit /b 1
where gh >nul 2>&1
if errorlevel 1 (
  echo ERROR: gh not on PATH
  exit /b 1
)
set "ZIP=dist\dlssnr2fx-%VER%-win64.zip"
echo == gh release create v%VER% ==
gh release view "v%VER%" >nul 2>&1
if not errorlevel 1 (
  echo ERROR: GitHub release v%VER% already exists
  exit /b 1
)
gh release create "v%VER%" "%ZIP%" --title "%VER%" --notes-file CHANGELOG.md
exit /b %ERRORLEVEL%

:build
echo == selftest ==
call "%~dp0build.bat" selftest
if errorlevel 1 exit /b 1
echo == OFX ==
call "%~dp0build.bat" plugin
if errorlevel 1 exit /b 1
echo == AEX ==
call "%~dp0build.bat" aex
if errorlevel 1 exit /b 1
exit /b 0

:pack
set "NAME=dlssnr2fx-%VER%-win64"
set "STAGE=dist\stage\%NAME%"
set "ZIP=dist\%NAME%.zip"
set "OFX_SRC=dist\dlssnr.ofx.bundle"
set "OFX_DST=%STAGE%\ofx\dlssnr.ofx.bundle"
set "AEX_DST=%STAGE%\aex"
set "FWD=dist\engine\nvngx.dll_dlssnr.dll"

if not exist "%OFX_SRC%\Contents\Win64\dlssnr.ofx" (
  echo ERROR: missing %OFX_SRC%\Contents\Win64\dlssnr.ofx — run release.bat ^(no args^) or build.bat plugin
  exit /b 1
)
if not exist "%OFX_SRC%\Contents\Win64\dlssnr2fx.dll" (
  echo ERROR: missing OFX engine dll
  exit /b 1
)
if not exist "dist\aex\dlssnr.aex" (
  echo ERROR: missing dist\aex\dlssnr.aex — run build.bat aex
  exit /b 1
)
if not exist "dist\aex\dlssnr2fx.dll" (
  echo ERROR: missing dist\aex\dlssnr2fx.dll
  exit /b 1
)
if not exist "%FWD%" (
  echo ERROR: missing %FWD% — run build.bat engine
  exit /b 1
)
if not exist fetch-runtime.bat (
  echo ERROR: fetch-runtime.bat missing
  exit /b 1
)

echo == pack %NAME%.zip ==
if exist "dist\stage" rmdir /s /q "dist\stage"
if exist "%ZIP%" del /q "%ZIP%"
mkdir "%OFX_DST%\Contents\Win64\runtime" 2>nul
mkdir "%AEX_DST%\runtime" 2>nul
mkdir "%STAGE%\runtime" 2>nul

copy /Y "%OFX_SRC%\Contents\Info.plist" "%OFX_DST%\Contents\Info.plist" >nul
if errorlevel 1 exit /b 1
copy /Y "%OFX_SRC%\Contents\Win64\dlssnr.ofx" "%OFX_DST%\Contents\Win64\dlssnr.ofx" >nul
if errorlevel 1 exit /b 1
copy /Y "%OFX_SRC%\Contents\Win64\dlssnr2fx.dll" "%OFX_DST%\Contents\Win64\dlssnr2fx.dll" >nul
if errorlevel 1 exit /b 1
copy /Y "dist\aex\dlssnr.aex" "%AEX_DST%\dlssnr.aex" >nul
if errorlevel 1 exit /b 1
copy /Y "dist\aex\dlssnr2fx.dll" "%AEX_DST%\dlssnr2fx.dll" >nul
if errorlevel 1 exit /b 1
copy /Y "%FWD%" "%STAGE%\runtime\nvngx.dll_dlssnr.dll" >nul
if errorlevel 1 exit /b 1
copy /Y "%FWD%" "%OFX_DST%\Contents\Win64\runtime\nvngx.dll_dlssnr.dll" >nul
if errorlevel 1 exit /b 1
copy /Y "%FWD%" "%AEX_DST%\runtime\nvngx.dll_dlssnr.dll" >nul
if errorlevel 1 exit /b 1
copy /Y fetch-runtime.bat "%STAGE%\fetch-runtime.bat" >nul
if errorlevel 1 exit /b 1
if exist README.md copy /Y README.md "%STAGE%\README.md" >nul
if exist CHANGELOG.md copy /Y CHANGELOG.md "%STAGE%\CHANGELOG.md" >nul

pushd dist\stage
tar.exe -a -c -f "..\%NAME%.zip" "%NAME%"
set "TAR_ERR=!ERRORLEVEL!"
popd
if not "!TAR_ERR!"=="0" (
  echo ERROR: tar failed to write %ZIP%
  exit /b 1
)
if not exist "%ZIP%" (
  echo ERROR: %ZIP% was not created
  exit /b 1
)
echo Packed %ZIP%
tar.exe -tf "%ZIP%"
exit /b 0
