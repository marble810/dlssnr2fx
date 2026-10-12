# Changelog

All notable changes to this project will be documented in this file.

The format is based on [Keep a Changelog](https://keepachangelog.com/en/1.1.0/),
and this project adheres to [Semantic Versioning](https://semver.org/spec/v2.0.0.html).

## [Unreleased]

## [0.1.1] - 2026-10-12

### Fixed

- After Effects classic rendering now checks out current-time parameter values instead of relying on a stale/default render array; View, look, highlight, guide, and advanced controls share the same render snapshot.
- Legacy Show Mask stays hidden and its saved values still render a mask. Every value checkout is checked in, including bypasses and partial failures.

### Added

- CPU-only AE parameter regression test (run automatically by `build.bat aex`) and an AE-host View smoke test.

## [0.1.0] - 2026-09-09

First Windows zip of the in-process DLSS Neural Rendering plug-in. NVIDIA
`nvngx_dlssnr.dll` is not redistributed; run `fetch-runtime.bat` after extract.

### Added

- In-process `dlssnr2fx.dll` engine shared by the OFX bundle and the After Effects plug-in.
- Self-built NGX DLSSNR forwarder (`nvngx.dll_dlssnr.dll`) and driver-store discovery for `nvngx.dll`.
- Optional depth and motion inputs, NVOF / Depth Anything V2 estimate paths, and a unified View list.
- Frame guidance with compositor-safe reset / lock modes.
- HDR residual reconstruction so neural output can keep the original float range.
- `fetch-runtime.bat` to download the GPU-family NR snippet plus DAV2 / ONNX Runtime.
- `release.bat` to compile OFX + AEX locally and pack `dlssnr2fx-<ver>-win64.zip`.

### Fixed

- HDR false color from clipping the neural proxy; highlights now reconstruct from the source peak.
- Cross-instance result reuse now keys on instance, inputs, and request mode.
- Nuke load / first-render crash: SRWLOCK instead of C++17 `std::mutex`.
- NGX lookup under current driver-store prefixes (`nv_dispi` / `nv_dispsi` / `nv_dispig`).
- OFX hosts that lie about `rowBytes` or committed size on proxy / mipmap renders (Houdini Copernicus and similar): clamp the world to a usable stride, catch pack/copy access violations, and skip tiny or mismatched frames.
- Depth / motion debug views no longer read past the packed buffer.

[Unreleased]: https://github.com/marble810/dlssnr2fx/compare/v0.1.1...HEAD
[0.1.1]: https://github.com/marble810/dlssnr2fx/compare/v0.1.0...v0.1.1
[0.1.0]: https://github.com/marble810/dlssnr2fx/releases/tag/v0.1.0
