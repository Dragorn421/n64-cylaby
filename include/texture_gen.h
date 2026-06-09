// SPDX-FileCopyrightText: 2026 Dragorn421
// SPDX-License-Identifier: CC0-1.0

#ifndef CYLABY_TEXTURE_GEN_H
#define CYLABY_TEXTURE_GEN_H

#include <stdbool.h>

#include <libdragon.h>

// kernel_filtering.c
void gaussian_kernel(float *kernel, int w, int h, float sigma);
void apply_kernel_wrap(float *out, const float *in, int w, int h,
                       const float *kernel, int kw, int kh);

// texture_convert.c
void tex_float_rgb_to_rgba16(void *out, float (*in)[3], int w, int h);
void tex_float_intensity_to_i4(void *out, float *in, int w, int h);

// texture_gen.c
struct generate_ground_texture_res {
    surface_t multitex_color;
    surface_t multitex_gray;
};
bool generate_ground_texture(struct generate_ground_texture_res *res);

// texture_utils.c
void tex_normalize_contrast(float *out, const float *in, int w, int h);

#endif
