// SPDX-FileCopyrightText: 2026 Dragorn421
// SPDX-License-Identifier: CC0-1.0

#include <assert.h>
#include <stdint.h>

#include <libdragon.h>

void tex_float_rgb_to_rgba16(void *out, float (*in)[3], int w, int h) {
    uint16_t *out16 = out;
    for (int y = 0; y < h; y++) {
        for (int x = 0; x < w; x++) {
            float *rgb = in[y * w + x];
            color_t color = {
                fm_roundf(rgb[0] * 255),
                fm_roundf(rgb[1] * 255),
                fm_roundf(rgb[2] * 255),
                255,
            };
            out16[y * w + x] = color_to_packed16(color);
        }
    }
}

void tex_float_intensity_to_i4(void *out, float *in, int w, int h) {
    assert(w % 2 == 0);
    uint8_t *out8 = out;
    for (int y = 0; y < h; y++) {
        for (int x = 0; x < w; x += 2) {
            float intensity1 = in[y * w + x];
            float intensity2 = in[y * w + x + 1];
            out8[y * w / 2 + x / 2] =
                (((uint8_t)fm_roundf(intensity1 * 255) >> 4) << 4) |
                (((uint8_t)fm_roundf(intensity2 * 255) >> 4) << 0);
        }
    }
}
