// SPDX-FileCopyrightText: 2026 Dragorn421
// SPDX-License-Identifier: CC0-1.0

#ifndef CYLABY_MODEL_H
#define CYLABY_MODEL_H

#include <stdint.h>

#include <libdragon.h>

#ifndef MIN
#define MIN(a, b) ((a) < (b) ? (a) : (b))
#endif
#ifndef MAX
#define MAX(a, b) ((a) > (b) ? (a) : (b))
#endif
#ifndef CLAMP
#define CLAMP(x, a, b) MAX(a, MIN(b, x))
#endif
#ifndef ROUNDF
#define ROUNDF(x) (int)(((x) >= 0.0f) ? ((x) + 0.5f) : ((x) - 0.5f))
#endif

#define MGFX_NRMF(x, y, z)                                                     \
    MGFX_NRM((int)CLAMP(ROUNDF((x) * 15.5f), -16.0f, 15.0f),                   \
             (int)CLAMP(ROUNDF((y) * 31.5f), -32.0f, 31.0f),                   \
             (int)CLAMP(ROUNDF((z) * 15.5f), -16.0f, 15.0f))

struct vertex {
    int16_t pos[3];
    uint16_t normal;
};

struct primitive {
    int material;
    struct vertex *vertices;
    uint16_t *indices;
    uint32_t index_count;
};

struct mesh {
    struct primitive **primitives;
    int n_primitives;
};

#endif
