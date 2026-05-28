// SPDX-FileCopyrightText: 2026 Dragorn421
// SPDX-License-Identifier: CC0-1.0

#include <assert.h>
#include <stdbool.h>
#include <stdint.h>
#include <stdlib.h>

#include "tower.h"

struct tower *malloc_tower(int n_floors, int segments_per_floor,
                           uint8_t wall_flags) {
    // malloc a single block of memory and then distribute it
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

/*
 * Maze generation
 */

enum connected_state {
    // The two nodes are disconnected and may be made connected
    CONNECTED_STATE_UNSET,
    // The two nodes are connected (and must not be made disconnected)
    CONNECTED_STATE_YES,
    // The two nodes are disconnected (and must not be made connected)
    CONNECTED_STATE_NO
};

/**
 * A NeighborNode is used to track and change the connection state from a Node
 * nodeA to another Node nodeB, using callback functions.
 * The callback functions take as arguments the tower and the nodeA Node
 * (/!\ not the nodeB Node which is stored in the NeighborNode).
 */
struct NeighborNode {
    struct Node *node;
    enum connected_state (*get_connected_state)(struct tower *, struct Node *);
    void (*set_connected_state)(struct tower *, struct Node *,
                                enum connected_state);
};
struct Node {
    // A node has up to 5 neighbors: up,down,left,right,corridor
#define MAX_N_NEIGHBORS 5
    struct NeighborNode neighbors[MAX_N_NEIGHBORS];
    int n_neighbors;
    int floor;
    int segment;
    /**
     * The class is the identifier of the connected component the Node belongs
     * to.  i.e. there is a path between two nodes if and only if their class is
     * the same.
     */
    int class;
};

/*
 * Implementation of callback functions for NeighborNode, for each direction of
 * neighbor.
 */

enum connected_state get_connected_state_corridor(struct tower *tower,
                                                  struct Node *node) {
    // Corridors always connect two nodes
    return CONNECTED_STATE_YES;
}

// Below: look at the wall flags of the same segment but in the floor below

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

// Above: look at the wall flags of the floor's segment

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

// Right: look at the wall flags in the same floor but of the segment to the
// right

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

// Left: look at the wall flags of the floor's segment

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

/**
 * Randomly resolve all _UNSET walls to either active/built or inactive/not
 * built.
 * Respects any wall already set in the tower data (for which the _UNSET flag is
 * not set).
 * The algorithm does not form loops: it works by iteratively marking walls as
 * inactive to connect connected components (loops may still be present in the
 * input, it won't break the algorithm).
 */
void build_tower_walls(struct tower *tower) {
    struct Node *nodes = malloc(sizeof(struct Node) * tower->n_floors *
                                tower->segments_per_floor);
    assert(nodes != NULL);
#define NODE(floor, segment)                                                   \
    (&nodes[(floor) * tower->segments_per_floor + (segment)])

    // Initialize nodes and their neighbors
    for (int i_floor = 0; i_floor < tower->n_floors; i_floor++) {
        struct tower_floor *floor = &tower->floors[i_floor];
        for (int i_segment = 0; i_segment < tower->segments_per_floor;
             i_segment++) {
            struct Node *node = NODE(i_floor, i_segment);
            // initialize node
            node->n_neighbors = 0;
            node->floor = i_floor;
            node->segment = i_segment;
            // corridor connection if any
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
            // bottom connection
            if (i_floor > 0) {
                node->neighbors[node->n_neighbors].node =
                    NODE(i_floor - 1, i_segment);
                node->neighbors[node->n_neighbors].get_connected_state =
                    get_connected_state_below;
                node->neighbors[node->n_neighbors].set_connected_state =
                    set_connected_state_below;
                node->n_neighbors++;
            }
            // upper connection
            if (i_floor + 1 < tower->n_floors) {
                node->neighbors[node->n_neighbors].node =
                    NODE(i_floor + 1, i_segment);
                node->neighbors[node->n_neighbors].get_connected_state =
                    get_connected_state_above;
                node->neighbors[node->n_neighbors].set_connected_state =
                    set_connected_state_above;
                node->n_neighbors++;
            }

            // right connection
            node->neighbors[node->n_neighbors].node =
                NODE(i_floor, (i_segment + 1) % tower->segments_per_floor);
            node->neighbors[node->n_neighbors].get_connected_state =
                get_connected_state_right;
            node->neighbors[node->n_neighbors].set_connected_state =
                set_connected_state_right;
            node->n_neighbors++;

            // left connection
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

    // Initialize the class for each node
    int i = 0;
    for (int i_floor = 0; i_floor < tower->n_floors; i_floor++) {
        for (int i_segment = 0; i_segment < tower->segments_per_floor;
             i_segment++) {
            NODE(i_floor, i_segment)->class = i++;
        }
    }

    // Further initialize the class for each node by identifying starting
    // connected components. For example nodes that are already connected
    // through a corridor, or if the tower already had some walls set (not
    // _UNSET) and missing.
    for (int i_floor = 0; i_floor < tower->n_floors; i_floor++) {
        for (int i_segment = 0; i_segment < tower->segments_per_floor;
             i_segment++) {
            struct Node *node = NODE(i_floor, i_segment);
            // merge_classes will hold the classes of neighbor nodes that are
            // already connected to the current node.
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
                // Replace the class of all nodes with a class in merge_classes,
                // with the current node's class.
                // This marks them as part of the same connected component.
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
    // Generate a maze by iteratively connecting the classes (connected
    // components) by marking walls as inactive (i.e. neighbors as connected).
    while (true) {
        // Find all the _UNSET walls that separate two distinct classes.
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

        // If we did not find any such walls (typically, because only a single
        // class remains), we're done.
        if (n_disjointed_neighbors == 0) {
            break;
        }

        // Pick a wall to mark as inactive/not built (i.e. a neighbor as
        // connected)
        int j = rand() % n_disjointed_neighbors;
        disjointed_neighbors[j].neighbor->set_connected_state(
            tower, disjointed_neighbors[j].node, CONNECTED_STATE_YES);
        // Merge the two previously separated, but now connected, classes
        // together.
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

    // Mark all undecided (_UNSET) walls as built (i.e. neighbors as
    // disconnected)
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
