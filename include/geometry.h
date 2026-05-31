// SPDX-FileCopyrightText: 2026 Dragorn421
// SPDX-License-Identifier: CC0-1.0

#ifndef CYLABY_GEOMETRY_H
#define CYLABY_GEOMETRY_H

#include "model.h"
#include "tower.h"

void geom_mesh_free_primitive(struct primitive *primitive);

struct primitive *generate_tower_geometry(struct tower *tower, float scale);
struct primitive *generate_tower_walls_geometry(struct tower *tower, float scale);

#endif
