// SPDX-FileCopyrightText: 2026 Dragorn421
// SPDX-License-Identifier: CC0-1.0

#include <float.h>
#include <stdbool.h>
#include <stdint.h>
#include <stdlib.h>

#include "texture_gen.h"

#ifndef ARRAY_COUNT
#define ARRAY_COUNT(arr) (sizeof(arr) / sizeof(arr[0]))
#endif

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

#define randf() ((rand() % 10001) / 10000.0f)
#define uniform(a, b) ((a) + ((b) - (a)) * randf())

#ifndef MIN
#define MIN(a, b) ((a) < (b) ? (a) : (b))
#endif
#ifndef MAX
#define MAX(a, b) ((a) > (b) ? (a) : (b))
#endif
#ifndef CLAMP
#define CLAMP(x, a, b) MAX(a, MIN(b, x))
#endif

struct set_pixel_ci8_ctx {
    uint8_t *im;
    int width;
    uint8_t value;
};

void set_pixel_ci8(void *uarg, int x, int y) {
    struct set_pixel_ci8_ctx *ctx = uarg;
    ctx->im[y * ctx->width + x] = ctx->value;
}

bool generate_flower_texture(struct generate_flower_texture_res *res) {
    uint8_t im[85][48] = {0};
    float palette[16][4];
    int palette_count = 0;
    memcpy(palette[palette_count++], (float[4]){0}, sizeof(float[4]));
    const float width = 48, height = 85;

    float heart_radius_max = width / 4;
    float heart_radius = uniform(0.3f * heart_radius_max, heart_radius_max);
    float petal_length_max = width / 2 - heart_radius;
    float petal_length = uniform(0.3f * petal_length_max, petal_length_max);
    int n_petals_min =
        (int)(2 * FM_PI * heart_radius / (2 * petal_length) * 0.6f);
    int n_petals =
        (int)fm_roundf(uniform(MAX(3, n_petals_min), n_petals_min + 4));
    float heart_x = width / 2;
    float heart_y_min = heart_radius + petal_length;
    float heart_y_max = width - heart_y_min;
    float heart_y = uniform(heart_y_min, heart_y_max);
    float stem_root_x = width / 2;
    float stem_root_y = height;
    float stem_width = uniform(width / 15, width / 6);

    float stem_color[4] = {0.4f, 0.6f + 0.4f * randf(), 0.2f, 1.0f};
    float heart_color[4] = {uniform(0.85f, 1.0f), uniform(0.85f, 1.0f), 0,
                            1.0f};
    uint8_t petal_colors[5][3] = {
        {245, 50, 50},   {211, 119, 166}, {134, 10, 59},
        {228, 235, 241}, {245, 142, 97},
    };
    int petal_color_index = rand() % 5;
    float petal_color[4];
    for (int c = 0; c < 3; c++) {
        petal_color[c] =
            petal_colors[petal_color_index][c] / 255.0f + uniform(-0.1f, 0.1f);
        petal_color[c] = CLAMP(petal_color[c], 0.0f, 1.0f);
    }
    petal_color[3] = 1.0f;

    struct set_pixel_ci8_ctx set_pixel_ci8_ctx = {im[0], width, 0};

    set_pixel_ci8_ctx.value = palette_count;
    memcpy(palette[palette_count++], stem_color, sizeof(float[4]));
    draw_vertical_line(width, height, heart_x, heart_y, stem_root_x,
                       stem_root_y, stem_width, set_pixel_ci8,
                       &set_pixel_ci8_ctx);

    set_pixel_ci8_ctx.value = palette_count;
    memcpy(palette[palette_count++], petal_color, sizeof(float[4]));

    float petal_theta_ini = uniform(0.0f, 2 * FM_PI);
    for (int i = 0; i < n_petals; i++) {
        float petal_theta = (float)i / n_petals * 2 * FM_PI + petal_theta_ini;
        float petal_theta_cos, petal_theta_sin;
        fm_sincosf(petal_theta, &petal_theta_sin, &petal_theta_cos);
        float petal_x = heart_x + heart_radius * petal_theta_cos;
        float petal_y = heart_y + heart_radius * petal_theta_sin;
        draw_disk(width, height, petal_x, petal_y, petal_length, set_pixel_ci8,
                  &set_pixel_ci8_ctx);
    }

    set_pixel_ci8_ctx.value = palette_count;
    memcpy(palette[palette_count++], heart_color, sizeof(float[4]));
    draw_disk(width, height, heart_x, heart_y, heart_radius, set_pixel_ci8,
              &set_pixel_ci8_ctx);

    void *tlut = malloc(palette_count * 2);
    if (tlut == NULL) {
        return false;
    }
    tex_float_rgba_to_rgba16(tlut, palette, palette_count, 1);
    uint8_t im_alphabled[85][48];
    uint16_t tlut_alphabled[256];
    int tlut_count_alphabled;
    alpha_bleeder(im[0], tlut, palette_count, width, height, im_alphabled[0],
                  tlut_alphabled, &tlut_count_alphabled);

    free(tlut);
    void *im_ci4 = malloc(width * height / 2);
    void *tlut_alphabled_malloced = malloc(tlut_count_alphabled * 2);
    if (im_ci4 == NULL || tlut_alphabled_malloced == NULL) {
        free(im_ci4);
        free(tlut_alphabled_malloced);
        return false;
    }
    tex_ci8_to_ci4(im_ci4, im_alphabled[0], width, height);
    memcpy(tlut_alphabled_malloced, tlut_alphabled, tlut_count_alphabled * 2);
    res->tex = surface_make_linear(im_ci4, FMT_CI4, width, height);
    res->tlut = tlut_alphabled_malloced;
    res->tlut_count = tlut_count_alphabled;
    return true;
}

/**
 * wrap x into [-y/2,y/2]
 */
static float wrap_scalar(float x, float y) {
    x = fm_fmodf(x, y);
    if (x < -y / 2) {
        x += y;
    } else if (x > y / 2) {
        x -= y;
    }
    return x;
}

bool generate_brick_texture(struct generate_brick_texture_res *res, int w,
                            int h) {
    float brick_width = w / fm_roundf(w / 13.0f);
    float brick_height = h / fm_roundf(h / 6.0f);

    int len_buf_points = 16;
    float (*points)[2] = malloc(sizeof(float[2]) * len_buf_points);
    if (points == NULL) {
        return false;
    }
    int n_points = 0;

    {
        float x = 0;
        float y = brick_height / 2;
        while (y < h) {
            x = fm_fmodf(x + brick_width / 2, brick_width);
            while (x < w) {
                if (n_points == len_buf_points) {
                    len_buf_points *= 2;
                    void *tmp =
                        realloc(points, sizeof(float[2]) * len_buf_points);
                    if (tmp == NULL) {
                        free(points);
                        return false;
                    }
                    points = tmp;
                }
                assert(n_points < len_buf_points);
                points[n_points][0] = x + uniform(-1.0f, 1.0f);
                points[n_points][1] = y + uniform(-1.0f, 1.0f);
                n_points++;
                x += brick_width;
            }
            y += brick_height;
        }
    }

    float coeff_x = 1 / brick_width;
    float coeff_y = 1 / brick_height;

    int *voronoi = malloc(sizeof(int) * w * h);
    if (voronoi == NULL) {
        free(points);
        return false;
    }

    for (int y = 0; y < h; y++) {
        for (int x = 0; x < w; x++) {
            int closest_point_index = -1;
            float closest_point_dist = FLT_MAX;
            float second_closest_point_dist = FLT_MAX;
            for (int i_point = 0; i_point < n_points; i_point++) {
                float dx = wrap_scalar(x - points[i_point][0], w);
                float dy = wrap_scalar(y - points[i_point][1], h);
                float dist = MAX(fabsf(dx * coeff_x), fabsf(dy * coeff_y));
                if (dist < closest_point_dist) {
                    second_closest_point_dist = closest_point_dist;
                    closest_point_index = i_point;
                    closest_point_dist = dist;
                } else if (dist < second_closest_point_dist) {
                    second_closest_point_dist = dist;
                }
            }
            if (fabsf(closest_point_dist - second_closest_point_dist) < 0.17) {
                voronoi[y * w + x] = -1;
            } else {
                voronoi[y * w + x] = closest_point_index;
            }
        }
    }

    free(points);

    float brick_colors[][3] = {
        {183 / 255.0f, 115 / 255.0f, 92 / 255.0f},
        {170 / 255.0f, 98 / 255.0f, 76 / 255.0f},
        {120 / 255.0f, 81 / 255.0f, 68 / 255.0f},
        {187 / 255.0f, 123 / 255.0f, 101 / 255.0f},
        {194 / 255.0f, 138 / 255.0f, 118 / 255.0f},
        {184 / 255.0f, 116 / 255.0f, 91 / 255.0f},
        {200 / 255.0f, 139 / 255.0f, 118 / 255.0f},
    };
    float mortar_color[3] = {202 / 255.0f, 190 / 255.0f, 183 / 255.0f};

    float (*im)[3] = malloc(sizeof(float[3]) * w * h);
    float *noise[3] = {
        malloc(sizeof(float) * w * h),
        malloc(sizeof(float) * w * h),
        malloc(sizeof(float) * w * h),
    };
    float *noise_smooth[3] = {
        malloc(sizeof(float) * w * h),
        malloc(sizeof(float) * w * h),
        malloc(sizeof(float) * w * h),
    };
    int *i_color_by_island = malloc(sizeof(int) * n_points);
    if (im == NULL || noise[0] == NULL || noise[1] == NULL ||
        noise[2] == NULL || noise_smooth[0] == NULL ||
        noise_smooth[1] == NULL || noise_smooth[2] == NULL ||
        i_color_by_island == NULL) {
        free(voronoi);
        free(im);
        free(noise[0]);
        free(noise[1]);
        free(noise[2]);
        free(noise_smooth[0]);
        free(noise_smooth[1]);
        free(noise_smooth[2]);
        free(i_color_by_island);
    }

    float kernel[3][3];
    gaussian_kernel(kernel[0], 3, 3, 0.7f);
    for (int c = 0; c < 3; c++) {
        for (int y = 0; y < h; y++) {
            for (int x = 0; x < w; x++) {
                noise[c][y * w + x] = uniform(-0.08f, 0.08f);
            }
        }
        apply_kernel_wrap(noise_smooth[c], noise[c], w, h, kernel[0], 3, 3);
    }

    free(noise[0]);
    free(noise[1]);
    free(noise[2]);

    for (int i = 0; i < n_points; i++) {
        i_color_by_island[i] = rand() % ARRAY_COUNT(brick_colors);
    }
    for (int y = 0; y < h; y++) {
        for (int x = 0; x < w; x++) {
            float *color;
            if (voronoi[y * w + x] == -1) {
                color = mortar_color;
            } else {
                color = brick_colors[i_color_by_island[voronoi[y * w + x]]];
            }
            for (int c = 0; c < 3; c++) {
                float f = color[c] + noise_smooth[c][y * w + x];
                im[y * w + x][c] = CLAMP(f, 0.0f, 1.0f);
            }
        }
    }

    free(voronoi);
    free(noise_smooth[0]);
    free(noise_smooth[1]);
    free(noise_smooth[2]);
    free(i_color_by_island);

    void *im_rgba16 = malloc(w * h * 2);
    if (im_rgba16 == NULL) {
        free(im);
        return false;
    }
    tex_float_rgb_to_rgba16(im_rgba16, im, w, h);

    free(im);

    res->tex = surface_make_linear(im_rgba16, FMT_RGBA16, w, h);
    return true;
}
