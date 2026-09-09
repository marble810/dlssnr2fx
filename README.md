# dlssnr2fx

In-process **DLSS Neural Rendering** plug-in for **OFX** (Natron, Nuke, …) and **After Effects**, Windows x64.

NVIDIA `nvngx_dlssnr.dll` is **not** in the source tree or the release zip. After you extract a release, run `fetch-runtime.bat` to pull the GPU-family snippet (and optional DAV2).

## Install from the zip

1. Extract `dlssnr2fx-<ver>-win64.zip`.
2. Run `fetch-runtime.bat` and pick the GPU family (`50` / `40` / `30`). It writes `runtime\` and also copies that runtime next to the OFX and AEX binaries when those folders are present.
3. **OFX** — copy `ofx\dlssnr.ofx.bundle` into an OFX plug-in directory, for example:
   - `C:\Program Files\Common Files\OFX\Plugins`
   - Natron: `%APPDATA%\INRIA\Natron\Plugins`
4. **After Effects** — copy `aex\dlssnr.aex`, `aex\dlssnr2fx.dll`, and `aex\runtime\` into:
   - `C:\Program Files\Adobe\Common\Plug-ins\7.0\MediaCore`

The plug-in walks parent folders looking for `runtime\` (needs `nvngx.dll_dlssnr.dll` + `nvngx_dlssnr.dll`). You can also set `DLSSNR_RUNTIME_DIR`.

Requires an NVIDIA RTX GPU. Depth Estimate needs `dav2_vits.onnx` and `onnxruntime.dll` (fetched by the same bat).

## Build from source

Visual Studio 2022 (MSVC) and the Windows SDK. After Effects builds also need the AE SDK (`AE_SDK_DIR`, or the local `AfterEffectsSDK_*` tree).

```bat
build.bat plugin     :: OFX bundle + engine → dist\dlssnr.ofx.bundle
build.bat aex        :: After Effects plug-in + engine → dist\aex
build.bat selftest   :: in-process tests (GPU test skips if no D3D12 adapter)
```

`fetch-runtime.bat [50|40|30|dav2] [--force]` downloads the NR snippet for this machine.

## Local release zip

No CI compile. Pack on the machine that has MSVC (and the AE SDK):

```bat
release.bat            :: selftest + OFX + AEX + dist\dlssnr2fx-<ver>-win64.zip
release.bat pack       :: zip existing dist\ (no compile)
release.bat publish    :: build, zip, then gh release create v<ver>
```

The zip always includes `fetch-runtime.bat`. It never includes `nvngx_dlssnr.dll`, the AE SDK, or `runtime\` caches.

Version is `VERSION`. Release notes live in `CHANGELOG.md` ([Keep a Changelog](https://keepachangelog.com/en/1.1.0/)).
