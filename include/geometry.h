// SPDX-FileCopyrightText: 2026 Dragorn421
// SPDX-License-Identifier: CC0-1.0

#ifndef CYLABY_GEOMETRY_H
#define CYLABY_GEOMETRY_H

#include "model.h"
#include "tower.h"

void geom_mesh_free_primitive(struct primitive *primitive);

struct primitive *generate_tower_geometry(struct tower *tower, float scale);
struct primitive *generate_tower_walls_geometry(struct tower *tower,
                                                float scale);
struct primitive *generate_ground_geometry(fm_vec2_t *from, fm_vec2_t *to,
                                           float z, fm_vec2_t *st_from,
                                           fm_vec2_t *st_to, int subdivX,
                                           int subdivY, fm_vec3_t *noise);

#endif
