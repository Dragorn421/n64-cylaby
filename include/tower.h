// SPDX-FileCopyrightText: 2026 Dragorn421
// SPDX-License-Identifier: CC0-1.0

#ifndef CYLABY_TOWER_H
#define CYLABY_TOWER_H

#include <stdint.h>

struct tower {
    struct tower_floor *floors;
    int n_floors;
    int segments_per_floor;
};

struct tower_floor {
#define WF_HORIZONTAL (1 << 0)
#define WF_VERTICAL (1 << 1)
#define WF_HORIZONTAL_UNSET (1 << 2)
#define WF_VERTICAL_UNSET (1 << 3)
    uint8_t *wall_flags;
    int corridor;
};

struct tower *malloc_tower(int n_floors, int segments_per_floor,
                           uint8_t wall_flags);
void free_tower(struct tower *tower);

void build_tower_walls(struct tower *tower);

#endif
