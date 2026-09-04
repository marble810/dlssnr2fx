#pragma once

#include <string>

// Locate the directory containing nvngx_dlssnr.dll. Search order:
// DLSSNR_RUNTIME_DIR, then runtime/ and the directory itself while walking up
// from the current plug-in module. Missing results are not permanently cached.
bool EnvTruthy(const wchar_t* name);
std::wstring FindRuntimeDir();
