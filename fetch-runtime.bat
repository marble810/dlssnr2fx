@echo off
setlocal EnableExtensions EnableDelayedExpansion
cd /d "%~dp0"

:: Fetch nvngx_dlssnr.dll for the local GPU family from RankFTW/rhi-repo.
:: Also fetches Depth Anything V2 Small ONNX + ONNX Runtime CPU (DAV2 estimate path).
::
::   fetch-runtime.bat              interactive, nvidia-smi suggests a default
::   fetch-runtime.bat 50           RTX 50 / Blackwell  (dlssnr-310.8.0)
::   fetch-runtime.bat 40           RTX 40 / Ada        (dlssnr-310.8.0-RTX40)
::   fetch-runtime.bat 30           RTX 20/30 FP16      (dlssnr-310.8.SF-v2, unsupported)
::   fetch-runtime.bat dav2         only DAV2 model + onnxruntime.dll
::   fetch-runtime.bat 40 --force   re-download even if the zip is cached

set "FAMILY="
set "FORCE=0"
set "DAV2_ONLY=0"

:parse_args
if "%~1"=="" goto args_done
if /i "%~1"=="-h" goto usage
if /i "%~1"=="--help" goto usage
if /i "%~1"=="/?" goto usage
if /i "%~1"=="--force" set "FORCE=1" & shift & goto parse_args
if /i "%~1"=="-f" set "FORCE=1" & shift & goto parse_args
if /i "%~1"=="50" set "FAMILY=50" & shift & goto parse_args
if /i "%~1"=="rtx50" set "FAMILY=50" & shift & goto parse_args
if /i "%~1"=="rtx-50" set "FAMILY=50" & shift & goto parse_args
if /i "%~1"=="40" set "FAMILY=40" & shift & goto parse_args
if /i "%~1"=="rtx40" set "FAMILY=40" & shift & goto parse_args
if /i "%~1"=="rtx-40" set "FAMILY=40" & shift & goto parse_args
if /i "%~1"=="30" set "FAMILY=30" & shift & goto parse_args
if /i "%~1"=="20" set "FAMILY=30" & shift & goto parse_args
if /i "%~1"=="rtx30" set "FAMILY=30" & shift & goto parse_args
if /i "%~1"=="rtx20" set "FAMILY=30" & shift & goto parse_args
if /i "%~1"=="sf" set "FAMILY=30" & shift & goto parse_args
if /i "%~1"=="dav2" set "DAV2_ONLY=1" & shift & goto parse_args
echo ERROR: unknown argument "%~1"
echo.
goto usage

:args_done
mkdir runtime 2>nul
mkdir nr_out 2>nul

if "!DAV2_ONLY!"=="1" goto dav2_only
if defined FAMILY goto family_ready

set "GPU_NAME="
set "DETECTED="
for /f "usebackq delims=" %%G in (`nvidia-smi --query-gpu=name --format=csv,noheader 2^>nul`) do (
  if not defined GPU_NAME set "GPU_NAME=%%G"
)

if defined GPU_NAME (
  echo Detected GPU: !GPU_NAME!
  echo !GPU_NAME! | findstr /i /c:"RTX 50" /c:"RTX50" /c:"5090" /c:"5080" /c:"5070" /c:"5060" >nul && set "DETECTED=50"
  if not defined DETECTED (
    echo !GPU_NAME! | findstr /i /c:"RTX 40" /c:"RTX40" /c:"4090" /c:"4080" /c:"4070" /c:"4060" /c:"4050" >nul && set "DETECTED=40"
  )
  if not defined DETECTED (
    echo !GPU_NAME! | findstr /i /c:"RTX 30" /c:"RTX30" /c:"RTX 20" /c:"RTX20" /c:"3090" /c:"3080" /c:"3070" /c:"3060" /c:"3050" /c:"2080" /c:"2070" /c:"2060" >nul && set "DETECTED=30"
  )
) else (
  echo nvidia-smi not found or no NVIDIA GPU reported.
)

echo.
echo Pick nvngx_dlssnr.dll from RankFTW/rhi-repo
echo   [1] RTX 50     dlssnr-310.8.0         Blackwell, stock NR
echo   [2] RTX 40     dlssnr-310.8.0-RTX40   Ada, patched NR
echo   [3] RTX 20/30  dlssnr-310.8.SF-v2     FP16, unsupported
echo   [Q] quit
echo.

set "DEFAULT_CHOICE=2"
if "!DETECTED!"=="50" set "DEFAULT_CHOICE=1"
if "!DETECTED!"=="40" set "DEFAULT_CHOICE=2"
if "!DETECTED!"=="30" set "DEFAULT_CHOICE=3"

if defined DETECTED (
  echo Recommended: [!DEFAULT_CHOICE!] RTX !DETECTED!   ^(Enter timeout 20s uses this^)
) else (
  echo No family detected. Default [2] RTX 40 in 20s.
)
echo.

choice /C 123Q /N /T 20 /D !DEFAULT_CHOICE! /M "Select 1/2/3/Q: "
if errorlevel 4 goto cancelled
if errorlevel 3 set "FAMILY=30" & goto family_ready
if errorlevel 2 set "FAMILY=40" & goto family_ready
if errorlevel 1 set "FAMILY=50" & goto family_ready
goto cancelled

:family_ready
if "!FAMILY!"=="50" (
  set "NR_LABEL=RTX 50 / Blackwell"
  set "NR_TAG=dlssnr-310.8.0"
  set "NR_ASSET=nvngx_dlssnr_310.8.0.zip"
  set "NR_CACHE=runtime\nr-rtx50.zip"
)
if "!FAMILY!"=="40" (
  set "NR_LABEL=RTX 40 / Ada"
  set "NR_TAG=dlssnr-310.8.0-RTX40"
  set "NR_ASSET=nvngx_dlssnr_310.8.0-RTX40.zip"
  set "NR_CACHE=runtime\nr-rtx40.zip"
)
if "!FAMILY!"=="30" (
  set "NR_LABEL=RTX 20/30 FP16 (unsupported)"
  set "NR_TAG=dlssnr-310.8.SF-v2"
  set "NR_ASSET=nvngx_dlssnr_310.8.SF-v2.zip"
  set "NR_CACHE=runtime\nr-rtx30.zip"
)
if not defined NR_TAG (
  echo ERROR: internal: unknown FAMILY=!FAMILY!
  exit /b 1
)

set "NR_URL=https://github.com/RankFTW/rhi-repo/releases/download/!NR_TAG!/!NR_ASSET!"

echo.
echo Family : !NR_LABEL!
echo DLL    : !NR_TAG!  -^> runtime\nvngx_dlssnr.dll
echo URL    : !NR_URL!
if "!FAMILY!"=="30" (
  echo.
  echo WARNING: 20/30 SF build is a different FP16 model, not a product SKU.
)
echo.

if "!FORCE!"=="1" if exist "!NR_CACHE!" (
  echo --force: deleting !NR_CACHE!
  del /q "!NR_CACHE!" >nul 2>nul
)

if not exist "!NR_CACHE!" if "!FAMILY!"=="40" if exist runtime\nr.zip (
  echo migrating runtime\nr.zip -^> !NR_CACHE!
  copy /y runtime\nr.zip "!NR_CACHE!" >nul
)

if not exist "!NR_CACHE!" (
  echo Downloading !NR_ASSET! ...
  curl.exe -L --retry 3 -o "!NR_CACHE!" "!NR_URL!"
  if errorlevel 1 (
    echo ERROR: NR zip download failed
    echo Manual: !NR_URL!
    exit /b 1
  )
) else echo using existing !NR_CACHE!

set "DLSSNR_NR_ZIP=!NR_CACHE!"
python -c "import os,zipfile,shutil,pathlib,sys; root=pathlib.Path('runtime'); nr=pathlib.Path(os.environ['DLSSNR_NR_ZIP']); tmp=root/'_nr'; shutil.rmtree(tmp, ignore_errors=True); tmp.mkdir(parents=True); zipfile.ZipFile(nr).extractall(tmp); files=[p for p in tmp.rglob('nvngx_dlssnr.dll')]; [shutil.copy2(p, root/p.name) for p in files]; shutil.rmtree(tmp, ignore_errors=True); print('copied', ', '.join(sorted({p.name for p in files}))); sys.exit(0 if files else 1)"
if errorlevel 1 (
  echo ERROR: python extract failed
  exit /b 1
)

if not exist runtime\nvngx_dlssnr.dll (
  echo ERROR: runtime\nvngx_dlssnr.dll missing after extract
  exit /b 1
)

> runtime\nr-family.txt echo !FAMILY!
echo !NR_TAG!>> runtime\nr-family.txt
echo !NR_LABEL!>> runtime\nr-family.txt

echo.
echo runtime ready  [!NR_LABEL!]
dir /b runtime\nvngx_dlssnr.dll
call :fetch_dav2
if errorlevel 1 echo WARNING: DAV2 fetch failed; Depth Estimate will stay Off until runtime\dav2_vits.onnx and onnxruntime.dll are present
exit /b 0

:cancelled
echo cancelled.
exit /b 2

:dav2_only
call :fetch_dav2
exit /b %ERRORLEVEL%

:fetch_dav2
set "DAV2_ONNX=runtime\dav2_vits.onnx"
set "DAV2_URL=https://huggingface.co/onnx-community/depth-anything-v2-small/resolve/main/onnx/model_fp16.onnx"
set "ORT_VER=1.29.0"
set "ORT_ZIP=runtime\onnxruntime-win-x64-!ORT_VER!.zip"
set "ORT_URL=https://github.com/microsoft/onnxruntime/releases/download/v!ORT_VER!/onnxruntime-win-x64-!ORT_VER!.zip"

echo.
echo DAV2  : onnx-community/depth-anything-v2-small  model_fp16.onnx
echo         -^> !DAV2_ONNX!
echo ORT   : onnxruntime-win-x64-!ORT_VER!  -^> runtime\onnxruntime.dll
echo.

if "!FORCE!"=="1" (
  if exist "!DAV2_ONNX!" del /q "!DAV2_ONNX!" >nul 2>nul
  if exist "!ORT_ZIP!" del /q "!ORT_ZIP!" >nul 2>nul
)

if not exist "!DAV2_ONNX!" (
  echo Downloading DAV2 ViT-S FP16 ONNX ...
  curl.exe -L --retry 3 -o "!DAV2_ONNX!" "!DAV2_URL!"
  if errorlevel 1 (
    echo ERROR: DAV2 ONNX download failed
    echo Manual: !DAV2_URL!
    exit /b 1
  )
) else echo using existing !DAV2_ONNX!

for %%I in ("!DAV2_ONNX!") do set "DAV2_SIZE=%%~zI"
if "!DAV2_SIZE!"=="" set "DAV2_SIZE=0"
if !DAV2_SIZE! LSS 1000000 (
  echo ERROR: !DAV2_ONNX! is too small ^(!DAV2_SIZE! bytes^), likely an HTML error page
  del /q "!DAV2_ONNX!" >nul 2>nul
  exit /b 1
)

if not exist "!ORT_ZIP!" (
  echo Downloading ONNX Runtime !ORT_VER! ...
  curl.exe -L --retry 3 -o "!ORT_ZIP!" "!ORT_URL!"
  if errorlevel 1 (
    echo ERROR: onnxruntime zip download failed
    echo Manual: !ORT_URL!
    exit /b 1
  )
) else echo using existing !ORT_ZIP!

if exist runtime\onnxruntime.dll if "!FORCE!"=="0" (
  echo using existing runtime\onnxruntime.dll
  dir /b runtime\dav2_vits.onnx runtime\onnxruntime.dll
  exit /b 0
)

set "DLSSNR_ORT_ZIP=!ORT_ZIP!"
python -c "import os,zipfile,shutil,pathlib,sys; root=pathlib.Path('runtime'); z=pathlib.Path(os.environ['DLSSNR_ORT_ZIP']); tmp=root/'_ort'; shutil.rmtree(tmp, ignore_errors=True); tmp.mkdir(parents=True); zipfile.ZipFile(z).extractall(tmp); names=('onnxruntime.dll','onnxruntime_providers_shared.dll'); files=[p for p in tmp.rglob('*') if p.name in names]; [shutil.copy2(p, root/p.name) for p in files]; shutil.rmtree(tmp, ignore_errors=True); print('copied', ', '.join(sorted({p.name for p in files}))); sys.exit(0 if any(p.name=='onnxruntime.dll' for p in files) else 1)"
if errorlevel 1 (
  echo ERROR: onnxruntime extract failed
  exit /b 1
)
if not exist runtime\onnxruntime.dll (
  echo ERROR: runtime\onnxruntime.dll missing after extract
  exit /b 1
)
dir /b runtime\dav2_vits.onnx runtime\onnxruntime.dll
exit /b 0

:usage
echo Usage: fetch-runtime.bat [50^|40^|30^|dav2] [--force]
echo.
echo   no args   detect GPU via nvidia-smi, then interactive menu
echo   50        RTX 50  nvngx_dlssnr from dlssnr-310.8.0
echo   40        RTX 40  nvngx_dlssnr from dlssnr-310.8.0-RTX40
echo   30        RTX 20/30 FP16 from dlssnr-310.8.SF-v2 ^(unsupported^)
echo   dav2      Depth Anything V2 Small FP16 ONNX + onnxruntime.dll
echo   --force   re-download cached zips/ONNX
echo.
echo NR zips are cached per family (runtime\nr-rtx40.zip etc.).
echo DAV2: huggingface.co/onnx-community/depth-anything-v2-small  model_fp16.onnx
echo ORT : github.com/microsoft/onnxruntime  onnxruntime-win-x64-1.29.0
exit /b 0
