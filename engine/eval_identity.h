#pragma once

#include "dlssnr2fx.h"
#include "session_policy.h"

#include <cstddef>
#include <cstdint>
#include <cstring>

// Content fingerprint. Do not hash a padded C struct, and do not use buffer
// addresses as identity — identical pixels at different pointers must match.
inline uint64_t NrHashBytes(const void* p, size_t n) {
  if (!p || n == 0) return 0;
  const uint8_t* b = static_cast<const uint8_t*>(p);
  uint64_t h = 14695981039346656037ull;
  size_t i = 0;
  for (; i + sizeof(uint64_t) <= n; i += sizeof(uint64_t)) {
    uint64_t w = 0;
    std::memcpy(&w, b + i, sizeof(w));
    h ^= w;
    h *= 1099511628211ull;
  }
  for (; i < n; ++i) {
    h ^= b[i];
    h *= 1099511628211ull;
  }
  return h;
}

struct NrEvalIdentity {
  uint32_t instance_id = 0;
  int64_t frame_id = 0;
  int32_t width = 0;
  int32_t height = 0;
  nr_pixel_format format = NR_PIX_RGBA8;
  nr_model model{};
  float detail = 0.f;
  float color = 0.f;
  int32_t hdr = 0;
  int32_t mvec_source = 0;
  int32_t depth_source = 0;
  int32_t nvof_quality = 0;
  int32_t nvof_hint = 0;
  int32_t dav2_interval = 0;
  float dav2_ema = 0.f;
  int32_t maps_only = 0;
  int32_t fg_mode = 0;
  int32_t has_prev = 0;
  int32_t has_mvec = 0;
  int32_t has_depth = 0;
  uint64_t rgba_hash = 0;
  uint64_t prev_hash = 0;
  uint64_t mvec_hash = 0;
  uint64_t depth_hash = 0;
};

inline size_t NrRgbaBytes(const nr_eval_desc& desc) {
  const size_t pixels = static_cast<size_t>(desc.width) * static_cast<size_t>(desc.height);
  return pixels * (desc.format == NR_PIX_RGBA32F ? sizeof(float) * 4u : 4u);
}

inline NrEvalIdentity NrIdentityFromDesc(const nr_eval_desc& desc) {
  NrEvalIdentity id{};
  const size_t pixels = static_cast<size_t>(desc.width) * static_cast<size_t>(desc.height);
  const size_t rgbaBytes = NrRgbaBytes(desc);
  id.instance_id = desc.instance_id;
  id.frame_id = desc.frame_id;
  id.width = desc.width;
  id.height = desc.height;
  id.format = desc.format;
  id.model = desc.model;
  id.detail = desc.detail;
  id.color = desc.color;
  id.hdr = desc.hdr;
  id.mvec_source = static_cast<int32_t>(desc.mvec_source);
  id.depth_source = static_cast<int32_t>(desc.depth_source);
  id.nvof_quality = static_cast<int32_t>(desc.nvof_quality);
  id.nvof_hint = desc.nvof_hint;
  id.dav2_interval = desc.dav2_interval;
  id.dav2_ema = desc.dav2_ema;
  id.maps_only = desc.maps_only;
  id.fg_mode = static_cast<int32_t>(desc.fg_mode);
  id.has_prev = desc.in_prev_rgba ? 1 : 0;
  id.has_mvec = desc.in_mvec ? 1 : 0;
  id.has_depth = desc.in_depth ? 1 : 0;
  id.rgba_hash = NrHashBytes(desc.in_rgba, rgbaBytes);
  id.prev_hash = desc.in_prev_rgba ? NrHashBytes(desc.in_prev_rgba, rgbaBytes) : 0;
  id.mvec_hash = desc.in_mvec ? NrHashBytes(desc.in_mvec, pixels * 2 * sizeof(float)) : 0;
  id.depth_hash = desc.in_depth ? NrHashBytes(desc.in_depth, pixels * sizeof(float)) : 0;
  return id;
}

inline bool NrIdentityEqual(const NrEvalIdentity& a, const NrEvalIdentity& b) {
  return a.instance_id == b.instance_id && a.frame_id == b.frame_id && a.width == b.width &&
         a.height == b.height && a.format == b.format && NrModelEqual(a.model, b.model) &&
         a.detail == b.detail && a.color == b.color && a.hdr == b.hdr &&
         a.mvec_source == b.mvec_source && a.depth_source == b.depth_source &&
         a.nvof_quality == b.nvof_quality && a.nvof_hint == b.nvof_hint &&
         a.dav2_interval == b.dav2_interval && a.dav2_ema == b.dav2_ema &&
         a.maps_only == b.maps_only && a.fg_mode == b.fg_mode && a.has_prev == b.has_prev &&
         a.has_mvec == b.has_mvec && a.has_depth == b.has_depth && a.rgba_hash == b.rgba_hash &&
         a.prev_hash == b.prev_hash && a.mvec_hash == b.mvec_hash && a.depth_hash == b.depth_hash;
}
