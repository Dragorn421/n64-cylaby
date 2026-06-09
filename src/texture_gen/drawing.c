// SPDX-FileCopyrightText: 2026 Dragorn421
// SPDX-License-Identifier: CC0-1.0

#include <math.h>

#include <libdragon.h>

#include "texture_gen.h"

void draw_vertical_line(int w, int h, float x1, float y1, float x2, float y2,
                        float width,
                        void (*set_pixel)(void *uarg, int x, int y),
                        void *uarg) {
    float y = y1;
    while (y <= y2) {
        int yn = (int)fm_roundf(y);
        if (0 <= yn && yn < h) {
            float x0 = (x2 - x1) / (y2 - y1) * (y - y1) + x1;
            float x = x0 - width / 2;
            while (x <= x0 + width / 2) {
                int xn = (int)fm_roundf(x);
                if (0 <= xn && xn < w) {
                    set_pixel(uarg, xn, yn);
                }
                x += 1;
            }
        }
        y += 1;
    }
}

void draw_disk(int w, int h, float center_x, float center_y, float radius,
               void (*set_pixel)(void *uarg, int x, int y), void *uarg) {
    float dx = -radius;
    while (dx <= radius) {
        float x = center_x + dx;
        int xn = (int)fm_roundf(x);
        if (0 <= xn && xn < w) {
            float line_half_height = sqrtf(radius * radius - dx * dx);
            float dy = -line_half_height;
            while (dy <= line_half_height) {
                float y = center_y + dy;
                int yn = (int)fm_roundf(y);
                if (0 <= yn && yn < h) {
                    set_pixel(uarg, xn, yn);
                }
                dy += 1;
            }
        }
        dx += 1;
    }
}
