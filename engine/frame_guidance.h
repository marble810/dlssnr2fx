#pragma once

#include <cstdint>

// Host-side temporal contract for Feature 18:
//   Coalesce — same instance+frame (+ matching output key): skip Evaluate
//   Continue — keep snippet history (Auto: next forward frame; Lock: any later frame)
//   Reset    — DLSSNR.Reset=1 (Always Reset, discontinuity, instance change)
enum class FrameGuidanceAction : uint8_t {
  Coalesce = 0,
  Reset = 1,
  Continue = 2
};

enum class FrameGuidanceMode : uint8_t {
  Auto = 0,
  AlwaysReset = 1,
  Lock = 2
};

inline FrameGuidanceAction DecideFrameGuidance(
    bool haveLast,
    uint32_t lastInstance,
    int64_t lastFrame,
    uint32_t instance,
    int64_t frame,
    int64_t step,
    bool forceReset,
    bool sameOutputKey,
    FrameGuidanceMode mode = FrameGuidanceMode::Auto,
    bool coalesce = true) {
  if (coalesce && haveLast && sameOutputKey)
    return FrameGuidanceAction::Coalesce;
  if (forceReset || !haveLast || lastInstance != instance)
    return FrameGuidanceAction::Reset;
  if (mode == FrameGuidanceMode::AlwaysReset)
    return FrameGuidanceAction::Reset;
  if (mode == FrameGuidanceMode::Lock)
    return FrameGuidanceAction::Continue;
  if (step <= 0)
    return FrameGuidanceAction::Reset;
  if (frame == lastFrame + step)
    return FrameGuidanceAction::Continue;
  return FrameGuidanceAction::Reset;
}
