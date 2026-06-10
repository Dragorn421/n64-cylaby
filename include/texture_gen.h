// SPDX-FileCopyrightText: 2026 Dragorn421
// SPDX-License-Identifier: CC0-1.0

#ifndef CYLABY_TEXTURE_GEN_H
#define CYLABY_TEXTURE_GEN_H

#include <stdbool.h>

#include <libdragon.h>

// alpha_bleeder.c
bool alpha_bleeder(uint8_t *im, uint16_t *tlut, int tlut_count, int width,
                   int height, uint8_t *out_im, uint16_t *out_tlut,
                   int *out_tlut_count);

// drawing.c
void draw_vertical_line(int w, int h, float x1, float y1, float x2, float y2,
                        float width,
                        void (*set_pixel)(void *uarg, int x, int y),
                        void *uarg);
void draw_disk(int w, int h, float center_x, float center_y, float radius,
               void (*set_pixel)(void *uarg, int x, int y), void *uarg);

// kernel_filtering.c
void gaussian_kernel(float *kernel, int w, int h, float sigma);
void apply_kernel_wrap(float *out, const float *in, int w, int h,
                       const float *kernel, int kw, int kh);

// texture_convert.c
void tex_float_rgb_to_rgba16(void *out, float (*in)[3], int w, int h);
void tex_float_rgba_to_rgba16(void *out, float (*in)[4], int w, int h);
void tex_float_intensity_to_i4(void *out, float *in, int w, int h);
void tex_ci8_to_ci4(void *out, uint8_t *in, int w, int h);

// texture_gen.c
struct generate_ground_texture_res {
    surface_t multitex_color;
    surface_t multitex_gray;
};
bool generate_ground_texture(struct generate_ground_texture_res *res);
struct generate_flower_texture_res {
    surface_t tex;
    void *tlut;
    int tlut_count;
};
bool generate_flower_texture(struct generate_flower_texture_res *res);
struct generate_brick_texture_res {
    surface_t tex;
};
bool generate_brick_texture(struct generate_brick_texture_res *res, int w, int h);

// texture_utils.c
void tex_normalize_contrast(float *out, const float *in, int w, int h);

#endif
