// SPDX-FileCopyrightText: 2026 Dragorn421
// SPDX-License-Identifier: CC0-1.0

#include <math.h>

#include "texture_gen.h"

/**
 * Compute and store a gaussian kernel into `float kernel[h][w]`
 */
void gaussian_kernel(float *kernel, int w, int h, float sigma) {
    for (int i = 0; i < h; i++) {
        for (int j = 0; j < w; j++) {
            int x = j - w / 2;
            int y = i - h / 2;
            kernel[i * w + j] = expf(-(x * x + y * y) / (sigma * sigma));
        }
    }
}

/**
 * Filter `float in[h][w]` with `float kernel[kh][kw]`
 * and store to `float out[h][w]`.
 *
 * Input boundaries are handled by wrapping the input,
 * so e.g. for "`in[i][w]`" then `in[i][0]` is used.
 *
 * out and in must refer to different memory locations
 */
void apply_kernel_wrap(float *out, const float *in, int w, int h,
                       const float *kernel, int kw, int kh) {
    for (int y = 0; y < h; y++) {
        for (int x = 0; x < w; x++) {
            float v = 0;
            float f = 0;
            for (int ky = 0; ky < kh; ky++) {
                for (int kx = 0; kx < kw; kx++) {
                    int j = (x + (kx - kw / 2) + w) % w;
                    int i = (y + (ky - kh / 2) + h) % h;
                    float v_in = in[i * w + j];
                    float k = kernel[ky * kw + kx];
                    v += v_in * k;
                    f += k;
                }
            }
            out[y * w + x] = v / f;
        }
    }
}
