// SPDX-FileCopyrightText: 2026 Dragorn421
// SPDX-License-Identifier: CC0-1.0

#include <stdbool.h>
#include <stdlib.h>

#include "texture_gen.h"

bool generate_ground_texture(struct generate_ground_texture_res *res) {
    float colors[4][3] = {
        {0.67843137, 0.77647059, 0.41960784},
        {0.32156863, 0.41960784, 0.19215686},
        {0.87058824, 0.77647059, 0.45098039},
        {0.61176471, 0.51764706, 0.29019608},
    };
    float im_color_facs[4][32][32];
    float kernel_color_fac[5][5];
    gaussian_kernel(kernel_color_fac[0], 5, 5, 1.5f);
    for (int k = 0; k < 4; k++) {
        float im_color_fac[32][32];
        for (int y = 0; y < 32; y++) {
            for (int x = 0; x < 32; x++) {
                im_color_fac[y][x] = (rand() % 256) / 255.0f;
            }
        }
        apply_kernel_wrap(im_color_facs[k][0], im_color_fac[0], 32, 32,
                          kernel_color_fac[0], 5, 5);
        tex_normalize_contrast(im_color_facs[k][0], im_color_facs[k][0], 32,
                               32);
    }
    float im_color[32][32][3];
    for (int y = 0; y < 32; y++) {
        for (int x = 0; x < 32; x++) {
            float rgb_sum[3] = {0};
            float w_sum = 0;
            for (int k = 0; k < 4; k++) {
                float w = im_color_facs[k][y][x];
                w = w * w * w * w;
                for (int c = 0; c < 3; c++) {
                    rgb_sum[c] += colors[k][c] * w;
                }
                w_sum += w;
            }
            for (int c = 0; c < 3; c++) {
                im_color[y][x][c] = rgb_sum[c] / w_sum;
            }
        }
    }
    float im_gray_noise[64][64];
    for (int y = 0; y < 64; y++) {
        for (int x = 0; x < 64; x++) {
            im_gray_noise[y][x] = (rand() % 256) / 255.0f;
        }
    }
    float im_gray[64][64];
    float kernel_gray[5][5];
    gaussian_kernel(kernel_gray[0], 5, 5, 1.0f);
    apply_kernel_wrap(im_gray[0], im_gray_noise[0], 64, 64, kernel_gray[0], 5,
                      5);
    tex_normalize_contrast(im_gray[0], im_gray[0], 64, 64);
    for (int y = 0; y < 64; y++) {
        for (int x = 0; x < 64; x++) {
            im_gray[y][x] = 0.5f + 0.5f * im_gray[y][x];
        }
    }
    void *im_color_rgba16 = malloc(32 * 32 * 2);
    void *im_gray_i4 = malloc(64 * 64 / 2);
    if (im_color_rgba16 == NULL || im_gray_i4 == NULL) {
        free(im_color_rgba16);
        free(im_gray_i4);
        return false;
    }
    tex_float_rgb_to_rgba16(im_color_rgba16, im_color[0], 32, 32);
    tex_float_intensity_to_i4(im_gray_i4, im_gray[0], 64, 64);
    res->multitex_color =
        surface_make_linear(im_color_rgba16, FMT_RGBA16, 32, 32);
    res->multitex_gray = surface_make_linear(im_gray_i4, FMT_I4, 64, 64);
    return true;
}
