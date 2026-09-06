#pragma once

#include <cstddef>
#include <cstdint>
#include <vector>

// Backward flow: displacement from the current pixel to its match in T-1,
// in source pixels. Output is width*height*2 floats (dx, dy).
void EstimateMvecRgba8(const uint8_t* current, const uint8_t* previous,
                       int width, int height, std::vector<float>& xy);
void EstimateMvecRgba32f(const float* current, const float* previous,
                         int width, int height, std::vector<float>& xy);
