// SPDX-FileCopyrightText: 2026 Dragorn421
// SPDX-License-Identifier: CC0-1.0

#ifndef CYLABY_POLYGONAL_COLLISION_H
#define CYLABY_POLYGONAL_COLLISION_H

#include <stdbool.h>

#include <libdragon.h>

struct polycol_mesh;

struct polycol_mesh *polycol_mesh_new(fm_vec3_t (*tris)[3], int n_tris);
void polycol_mesh_free(struct polycol_mesh *mesh);

bool polycol_raycast_down(const struct polycol_mesh *mesh, const fm_vec3_t *pos,
                          float *intersectZ);

#endif
