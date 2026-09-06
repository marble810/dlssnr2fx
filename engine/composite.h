#pragma once

#include <cstddef>
#include <cstdint>

void CompositeRgba8(const uint8_t* original, const uint8_t* neural, uint8_t* output,
                    size_t pixels, float detail, float color, bool hdr);
void CompositeRgba32f(const float* original, const float* neural, float* output,
                      size_t pixels, float detail, float color, bool hdr);
