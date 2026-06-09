// SPDX-FileCopyrightText: 2026 Dragorn421
// SPDX-License-Identifier: CC0-1.0

#include "texture_gen.h"

/**
 * in and out may refer to the same memory
 */
void tex_normalize_contrast(float *out, const float *in, int w, int h) {
    float min = in[0], max = in[0];
    for (int k = 0; k < w * h; k++) {
        if (in[k] < min) {
            min = in[k];
        }
        if (in[k] > max) {
            max = in[k];
        }
    }
    if (min == max) {
        return;
    }
    for (int k = 0; k < w * h; k++) {
        out[k] = (in[k] - min) / (max - min);
    }
}
