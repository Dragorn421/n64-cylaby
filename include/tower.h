// SPDX-FileCopyrightText: 2026 Dragorn421
// SPDX-License-Identifier: CC0-1.0

#ifndef CYLABY_TOWER_H
#define CYLABY_TOWER_H

#include <stdint.h>

/**
 * Main tower struct. Split into n_floors floors, and each floor into
 * segments_per_floor segments.
 */
struct tower {
    struct tower_floor *floors;
    int n_floors;
    int segments_per_floor;
};

/**
 * Description for a single floor of a tower.
 */
struct tower_floor {
#define WF_HORIZONTAL (1 << 0)
#define WF_VERTICAL (1 << 1)
// The _UNSET flags indicate to the maze generation function (build_tower_walls)
// that it can choose whether to put a wall there.
#define WF_HORIZONTAL_UNSET (1 << 2)
#define WF_VERTICAL_UNSET (1 << 3)
    /**
     * Array of wall flags of length segments_per_floor.
     * Each segment's flags indicate the wall state
     * - to the left for vertical walls (walls spanning vertically and blocking
     * horizontal movement)
     * - to the above for horizontal walls (walls spanning horizontally and
     * blocking vertical movement)
     */
    uint8_t *wall_flags;
    /**
     * -1 for no corridor, or in the range [0;segments_per_floor/2) to indicate
     * a corridor going through this floor.
     */
    int corridor;
};

struct tower *malloc_tower(int n_floors, int segments_per_floor,
                           uint8_t wall_flags);
void free_tower(struct tower *tower);

void build_tower_walls(struct tower *tower);

#endif
