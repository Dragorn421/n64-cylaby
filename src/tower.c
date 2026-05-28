// SPDX-FileCopyrightText: 2026 Dragorn421
// SPDX-License-Identifier: CC0-1.0

#include <assert.h>
#include <stdbool.h>
#include <stdint.h>
#include <stdlib.h>

#include "tower.h"

struct tower *malloc_tower(int n_floors, int segments_per_floor,
                           uint8_t wall_flags) {
    uintptr_t ptr = (uintptr_t)malloc(
        sizeof(struct tower) + sizeof(struct tower_floor) * n_floors +
        sizeof(uint8_t) * segments_per_floor * n_floors);
    if (ptr == 0) {
        return NULL;
    }
    struct tower *tower = (void *)ptr;
    ptr += sizeof(struct tower);
    tower->floors = (void *)ptr;
    ptr += sizeof(struct tower_floor) * n_floors;
    tower->n_floors = n_floors;
    tower->segments_per_floor = segments_per_floor;
    for (int i = 0; i < n_floors; i++) {
        tower->floors[i].corridor = -1;
        tower->floors[i].wall_flags = (void *)ptr;
        ptr += sizeof(uint8_t) * segments_per_floor;
        for (int j = 0; j < segments_per_floor; j++) {
            tower->floors[i].wall_flags[j] = wall_flags;
        }
    }
    return tower;
}

void free_tower(struct tower *tower) { free(tower); }

enum connected_state {
    CONNECTED_STATE_UNSET,
    CONNECTED_STATE_YES,
    CONNECTED_STATE_NO
};

struct NeighborNode {
    struct Node *node;
    enum connected_state (*get_connected_state)(struct tower *, struct Node *);
    void (*set_connected_state)(struct tower *, struct Node *,
                                enum connected_state);
};
struct Node {
#define MAX_N_NEIGHBORS 5
    struct NeighborNode neighbors[MAX_N_NEIGHBORS];
    int n_neighbors;
    int floor;
    int segment;
    int class;
};

enum connected_state get_connected_state_corridor(struct tower *tower,
                                                  struct Node *node) {
    return CONNECTED_STATE_YES;
}

enum connected_state get_connected_state_below(struct tower *tower,
                                               struct Node *node) {
    uint8_t wall_flags =
        tower->floors[node->floor - 1].wall_flags[node->segment];
    return wall_flags & WF_HORIZONTAL_UNSET ? CONNECTED_STATE_UNSET
           : wall_flags & WF_HORIZONTAL     ? CONNECTED_STATE_YES
                                            : CONNECTED_STATE_NO;
}

void set_connected_state_below(struct tower *tower, struct Node *node,
                               enum connected_state state) {
    uint8_t *wall_flags =
        &tower->floors[node->floor - 1].wall_flags[node->segment];
    *wall_flags &= ~(WF_HORIZONTAL | WF_HORIZONTAL_UNSET);
    switch (state) {
    case CONNECTED_STATE_UNSET:
        *wall_flags |= WF_HORIZONTAL_UNSET;
        break;
    case CONNECTED_STATE_YES:
        break;
    case CONNECTED_STATE_NO:
        *wall_flags |= WF_HORIZONTAL;
        break;
    }
}

enum connected_state get_connected_state_above(struct tower *tower,
                                               struct Node *node) {
    uint8_t wall_flags = tower->floors[node->floor].wall_flags[node->segment];
    return wall_flags & WF_HORIZONTAL_UNSET ? CONNECTED_STATE_UNSET
           : wall_flags & WF_HORIZONTAL     ? CONNECTED_STATE_YES
                                            : CONNECTED_STATE_NO;
}

void set_connected_state_above(struct tower *tower, struct Node *node,
                               enum connected_state state) {
    uint8_t *wall_flags = &tower->floors[node->floor].wall_flags[node->segment];
    *wall_flags &= ~(WF_HORIZONTAL | WF_HORIZONTAL_UNSET);
    switch (state) {
    case CONNECTED_STATE_UNSET:
        *wall_flags |= WF_HORIZONTAL_UNSET;
        break;
    case CONNECTED_STATE_YES:
        break;
    case CONNECTED_STATE_NO:
        *wall_flags |= WF_HORIZONTAL;
        break;
    }
}

enum connected_state get_connected_state_right(struct tower *tower,
                                               struct Node *node) {
    uint8_t wall_flags =
        tower->floors[node->floor]
            .wall_flags[(node->segment + 1) % tower->segments_per_floor];
    return wall_flags & WF_VERTICAL_UNSET ? CONNECTED_STATE_UNSET
           : wall_flags & WF_VERTICAL     ? CONNECTED_STATE_YES
                                          : CONNECTED_STATE_NO;
}

void set_connected_state_right(struct tower *tower, struct Node *node,
                               enum connected_state state) {
    uint8_t *wall_flags =
        &tower->floors[node->floor]
             .wall_flags[(node->segment + 1) % tower->segments_per_floor];
    *wall_flags &= ~(WF_VERTICAL | WF_VERTICAL_UNSET);
    switch (state) {
    case CONNECTED_STATE_UNSET:
        *wall_flags |= WF_VERTICAL_UNSET;
        break;
    case CONNECTED_STATE_YES:
        break;
    case CONNECTED_STATE_NO:
        *wall_flags |= WF_VERTICAL;
        break;
    }
}

enum connected_state get_connected_state_left(struct tower *tower,
                                              struct Node *node) {
    uint8_t wall_flags = tower->floors[node->floor].wall_flags[node->segment];
    return wall_flags & WF_VERTICAL_UNSET ? CONNECTED_STATE_UNSET
           : wall_flags & WF_VERTICAL     ? CONNECTED_STATE_YES
                                          : CONNECTED_STATE_NO;
}

void set_connected_state_left(struct tower *tower, struct Node *node,
                              enum connected_state state) {
    uint8_t *wall_flags = &tower->floors[node->floor].wall_flags[node->segment];
    *wall_flags &= ~(WF_VERTICAL | WF_VERTICAL_UNSET);
    switch (state) {
    case CONNECTED_STATE_UNSET:
        *wall_flags |= WF_VERTICAL_UNSET;
        break;
    case CONNECTED_STATE_YES:
        break;
    case CONNECTED_STATE_NO:
        *wall_flags |= WF_VERTICAL;
        break;
    }
}

void build_tower_walls(struct tower *tower) {
    struct Node *nodes = malloc(sizeof(struct Node) * tower->n_floors *
                                tower->segments_per_floor);
    assert(nodes != NULL);
#define NODE(floor, segment)                                                   \
    (&nodes[(floor) * tower->segments_per_floor + (segment)])

    for (int i_floor = 0; i_floor < tower->n_floors; i_floor++) {
        struct tower_floor *floor = &tower->floors[i_floor];
        for (int i_segment = 0; i_segment < tower->segments_per_floor;
             i_segment++) {
            struct Node *node = NODE(i_floor, i_segment);
            node->n_neighbors = 0;
            node->floor = i_floor;
            node->segment = i_segment;
            if (floor->corridor != -1 &&
                (floor->corridor == i_segment ||
                 ((floor->corridor + tower->segments_per_floor / 2) %
                      tower->segments_per_floor ==
                  i_segment))) {
                node->neighbors[node->n_neighbors].node =
                    NODE(i_floor, (i_segment + tower->segments_per_floor / 2) %
                                      tower->segments_per_floor);
                node->neighbors[node->n_neighbors].get_connected_state =
                    get_connected_state_corridor;
                node->neighbors[node->n_neighbors].set_connected_state = NULL;
                node->n_neighbors++;
            }
            if (i_floor > 0) {
                node->neighbors[node->n_neighbors].node =
                    NODE(i_floor - 1, i_segment);
                node->neighbors[node->n_neighbors].get_connected_state =
                    get_connected_state_below;
                node->neighbors[node->n_neighbors].set_connected_state =
                    set_connected_state_below;
                node->n_neighbors++;
            }
            if (i_floor + 1 < tower->n_floors) {
                node->neighbors[node->n_neighbors].node =
                    NODE(i_floor + 1, i_segment);
                node->neighbors[node->n_neighbors].get_connected_state =
                    get_connected_state_above;
                node->neighbors[node->n_neighbors].set_connected_state =
                    set_connected_state_above;
                node->n_neighbors++;
            }

            node->neighbors[node->n_neighbors].node =
                NODE(i_floor, (i_segment + 1) % tower->segments_per_floor);
            node->neighbors[node->n_neighbors].get_connected_state =
                get_connected_state_right;
            node->neighbors[node->n_neighbors].set_connected_state =
                set_connected_state_right;
            node->n_neighbors++;

            node->neighbors[node->n_neighbors].node =
                NODE(i_floor, (i_segment - 1 + tower->segments_per_floor) %
                                  tower->segments_per_floor);
            node->neighbors[node->n_neighbors].get_connected_state =
                get_connected_state_left;
            node->neighbors[node->n_neighbors].set_connected_state =
                set_connected_state_left;
            node->n_neighbors++;
        }
    }

    int i = 0;
    for (int i_floor = 0; i_floor < tower->n_floors; i_floor++) {
        for (int i_segment = 0; i_segment < tower->segments_per_floor;
             i_segment++) {
            NODE(i_floor, i_segment)->class = i++;
        }
    }

    for (int i_floor = 0; i_floor < tower->n_floors; i_floor++) {
        for (int i_segment = 0; i_segment < tower->segments_per_floor;
             i_segment++) {
            struct Node *node = NODE(i_floor, i_segment);
            int merge_classes[MAX_N_NEIGHBORS];
            int n_merge_classes = 0;
            for (int i_neighbor = 0; i_neighbor < node->n_neighbors;
                 i_neighbor++) {
                struct NeighborNode *neighbor = &node->neighbors[i_neighbor];
                if (neighbor->get_connected_state(tower, node) ==
                    CONNECTED_STATE_YES) {
                    merge_classes[n_merge_classes++] = neighbor->node->class;
                }
            }
            if (n_merge_classes != 0) {
                for (int j_floor = 0; j_floor < tower->n_floors; j_floor++) {
                    for (int j_segment = 0;
                         j_segment < tower->segments_per_floor; j_segment++) {
                        struct Node *other = NODE(j_floor, j_segment);
                        for (int k = 0; k < n_merge_classes; k++) {
                            if (other->class == merge_classes[k]) {
                                other->class = node->class;
                                break;
                            }
                        }
                    }
                }
            }
        }
    }

    struct disjointed_neighbor_elem {
        struct Node *node;
        struct NeighborNode *neighbor;
    };
    struct disjointed_neighbor_elem *disjointed_neighbors =
        malloc(sizeof(struct disjointed_neighbor_elem) * tower->n_floors *
               tower->segments_per_floor * MAX_N_NEIGHBORS);
    assert(disjointed_neighbors != NULL);
    int n_disjointed_neighbors;
    while (true) {
        n_disjointed_neighbors = 0;
        for (int i_floor = 0; i_floor < tower->n_floors; i_floor++) {
            for (int i_segment = 0; i_segment < tower->segments_per_floor;
                 i_segment++) {
                struct Node *node = NODE(i_floor, i_segment);
                for (int i_neighbor = 0; i_neighbor < node->n_neighbors;
                     i_neighbor++) {
                    struct NeighborNode *neighbor =
                        &node->neighbors[i_neighbor];
                    if (node->class != neighbor->node->class &&
                        neighbor->get_connected_state(tower, node) ==
                            CONNECTED_STATE_UNSET) {
                        disjointed_neighbors[n_disjointed_neighbors++] =
                            (struct disjointed_neighbor_elem){node, neighbor};
                    }
                }
            }
        }

        if (n_disjointed_neighbors == 0) {
            break;
        }

        int j = rand() % n_disjointed_neighbors;
        disjointed_neighbors[j].neighbor->set_connected_state(
            tower, disjointed_neighbors[j].node, CONNECTED_STATE_YES);
        int remove_class = disjointed_neighbors[j].neighbor->node->class;
        int new_class = disjointed_neighbors[j].node->class;
        for (int i_floor = 0; i_floor < tower->n_floors; i_floor++) {
            for (int i_segment = 0; i_segment < tower->segments_per_floor;
                 i_segment++) {
                struct Node *node = NODE(i_floor, i_segment);
                if (node->class == remove_class) {
                    node->class = new_class;
                }
            }
        }
    }

    for (int i_floor = 0; i_floor < tower->n_floors; i_floor++) {
        for (int i_segment = 0; i_segment < tower->segments_per_floor;
             i_segment++) {
            struct Node *node = NODE(i_floor, i_segment);
            for (int i_neighbor = 0; i_neighbor < node->n_neighbors;
                 i_neighbor++) {
                struct NeighborNode *neighbor = &node->neighbors[i_neighbor];
                if (neighbor->get_connected_state(tower, node) ==
                    CONNECTED_STATE_UNSET) {
                    neighbor->set_connected_state(tower, node,
                                                  CONNECTED_STATE_NO);
                }
            }
        }
    }

    free(disjointed_neighbors);
#undef NODE
    free(nodes);
}
