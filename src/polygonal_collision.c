// SPDX-FileCopyrightText: 2026 Dragorn421
// SPDX-License-Identifier: CC0-1.0

#include <stdbool.h>
#include <stdlib.h>

#include <libdragon.h>

#include "polygonal_collision.h"

#ifndef MAX
#define MAX(a, b) ((a) > (b) ? (a) : (b))
#endif

struct polycol_mesh {
    /**
     * Tris are sorted by max(vertices.x)
     */
    struct polycol_tri *tris;
    int n_tris;
};

/**
 * A point p belongs to a plane <=> dot(normal,p) == d
 */
struct polycol_plane {
    fm_vec3_t normal;
    float d;
};

struct polycol_tri {
    /**
     * Vertices are ordered such that vertices[0].x = min(vertices.x)
     */
    fm_vec3_t vertices[3];
    struct polycol_plane plane;
};

float get_tri_max_x(const struct polycol_tri *tri) {
    // Since vertices[0].x is min(vertices.x),
    // max(vertices.x) is one of the other two
    return MAX(tri->vertices[1].x, tri->vertices[2].x);
}

int compare_tris_max_x(const void *a_, const void *b_) {
    const struct polycol_tri *a = a_, *b = b_;
    float a_max_x = get_tri_max_x(a);
    float b_max_x = get_tri_max_x(b);
    return a_max_x < b_max_x ? -1 : a_max_x == b_max_x ? 0 : 1;
}

struct polycol_mesh *polycol_mesh_new(fm_vec3_t (*tris)[3], int n_tris) {
    uintptr_t ptr = (uintptr_t)malloc(sizeof(struct polycol_mesh) +
                                      sizeof(struct polycol_tri) * n_tris);
    if (ptr == 0) {
        return NULL;
    }
    struct polycol_mesh *mesh = (void *)ptr;
    ptr += sizeof(struct polycol_mesh);
    mesh->tris = (void *)ptr;
    mesh->n_tris = n_tris;
    for (int i = 0; i < n_tris; i++) {
        int k_vert_min_x = 0;
        for (int k = 1; k < 3; k++) {
            if (tris[i][k].x < tris[i][k_vert_min_x].x) {
                k_vert_min_x = k;
            }
        }
        for (int k = 0; k < 3; k++) {
            mesh->tris[i].vertices[k] = tris[i][(k_vert_min_x + k) % 3];
        }
        fm_vec3_t *verts = mesh->tris[i].vertices;
        fm_vec3_t a, b, n;
        fm_vec3_sub(&a, &verts[1], &verts[0]);
        fm_vec3_sub(&b, &verts[2], &verts[0]);
        fm_vec3_cross(&n, &a, &b);
        float f = fm_vec3_len(&n);
        if (f < FM_EPSILON) {
            n = (fm_vec3_t){{0, 0, 1}};
        } else {
            fm_vec3_scale(&n, &n, 1.0f / f);
        }
        mesh->tris[i].plane.normal = n;
        mesh->tris[i].plane.d = fm_vec3_dot(&n, &verts[0]);
    }
    qsort(mesh->tris, mesh->n_tris, sizeof(struct polycol_tri),
          compare_tris_max_x);
    return mesh;
}

void polycol_mesh_free(struct polycol_mesh *mesh) { free(mesh); }

bool polycol_tri_raycast_down(const struct polycol_tri *tri, float x, float y,
                              float *z) {
    // intersection between tri plane and vertical line (x,y,*)
    // (x,y,z_tri_plane) in tri plane <=> x*nx + y*ny + z_tri_plane*nz == d
    const fm_vec3_t *n = &tri->plane.normal;
    if (n->z <= 0.0f) {
        // if nz < 0 the triangle faces downwards
        // if nz == 0 the triangle is vertical
        return false;
    }
    float z_tri_plane = (tri->plane.d - x * n->x - y * n->y) / n->z;
    if (z_tri_plane > *z) {
        return false;
    }
    // determine if the point (x,y,z_tri_plane) is in tri
    // p[i] = vertices[i] - (x,y,z_tri_plane)
    // (x,y,z_tri_plane) is in tri
    //  <=> dot(n, cross(p[i], p[(i+1) % 3])) >= 0  for i=0,1,2
    //  <=> cross(p[i], p[(i+1) % 3]).z * nz >= 0  for i=0,1,2
    //      (since cross(p[i], p[(i+1) % 3]) and n are colinear and nz != 0)
    //  <=> cross(p[i], p[(i+1) % 3]).z >= 0  for i=0,1,2
    //      (since nz > 0)
    bool is_in_tri = true;
    for (int i = 0; i < 3; i++) {
        const fm_vec3_t *v0 = &tri->vertices[i],
                        *v1 = &tri->vertices[(i + 1) % 3];
        fm_vec2_t p0 = {{v0->x - x, v0->y - y}};
        fm_vec2_t p1 = {{v1->x - x, v1->y - y}};
        if (p0.x * p1.y - p0.y * p1.x < 0.0f) {
            is_in_tri = false;
        }
    }
    if (is_in_tri) {
        *z = z_tri_plane;
        return true;
    } else {
        return false;
    }
}

bool polycol_raycast_down(const struct polycol_mesh *mesh, const fm_vec3_t *pos,
                          float *intersectZ) {
    const float x = pos->x;
    // Bisect tris to find the index of the first tri with
    // max(vertices.x) >= x
    int i_tri_first_a = 0, i_tri_first_b = mesh->n_tris;
    while (i_tri_first_a != i_tri_first_b) {
        int i = (i_tri_first_a + i_tri_first_b) / 2;
        if (get_tri_max_x(&mesh->tris[i]) < x) {
            i_tri_first_a = i + 1;
        } else {
            i_tri_first_b = i;
        }
    }
    // Further filter all tris such that min(vertices.x) <= x
    int i_tri_first = i_tri_first_a;
    struct polycol_tri **tris_to_check =
        malloc(sizeof(struct polycol_tri *) * (mesh->n_tris - i_tri_first));
    int n_tris_to_check = 0;
    if (tris_to_check == NULL) {
        return false;
    }
    for (int i = i_tri_first; i < mesh->n_tris; i++) {
        if (mesh->tris[i].vertices[0].x <= x) {
            tris_to_check[n_tris_to_check++] = &mesh->tris[i];
        }
    }
    // tris_to_check now contains all triangles such that
    // min(vertices.x) <= x <= max(vertices.x)
    float z = pos->z;
    bool hit = false;
    for (int i = 0; i < n_tris_to_check; i++) {
        if (polycol_tri_raycast_down(tris_to_check[i], pos->x, pos->y, &z)) {
            hit = true;
        }
    }
    free(tris_to_check);
    if (hit) {
        *intersectZ = z;
        return true;
    } else {
        return false;
    }
}
