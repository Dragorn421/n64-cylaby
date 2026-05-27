// SPDX-FileCopyrightText: 2026 Dragorn421
// SPDX-License-Identifier: CC0-1.0

#include <stdint.h>
#include <stdlib.h>
#include <string.h>

#include <libdragon.h>

#include "geometry.h"
#include "model.h"
#include "tower.h"

struct geom_polygon {
#define POLY_MAX_VERTS 4
    int vertices[POLY_MAX_VERTS];
    int n_vertices;
};

struct geom_mesh {
    fm_vec3_t *vertices;
    struct geom_polygon *polygons;
    int max_vertices;
    int n_vertices;
    int max_polygons;
    int n_polygons;
};

struct geom_mesh *malloc_mesh(int max_vertices_ini, int max_polygons_ini) {
    struct geom_mesh *mesh = malloc(sizeof(struct geom_mesh));
    if (mesh == NULL) {
        return NULL;
    }
    mesh->vertices = malloc(sizeof(fm_vec3_t) * max_vertices_ini);
    mesh->polygons = malloc(sizeof(struct geom_polygon) * max_polygons_ini);
    if (mesh->vertices == NULL || mesh->polygons == NULL) {
        free(mesh->vertices);
        free(mesh->polygons);
        free(mesh);
        return NULL;
    }
    mesh->max_vertices = max_vertices_ini;
    mesh->n_vertices = 0;
    mesh->max_polygons = max_polygons_ini;
    mesh->n_polygons = 0;
    return mesh;
}

void free_mesh(struct geom_mesh *mesh) {
    free(mesh->vertices);
    free(mesh->polygons);
    free(mesh);
}

fm_vec3_t *add_vertex(struct geom_mesh *mesh, int *i_vertex_p) {
    if (mesh->n_vertices == mesh->max_vertices) {
        int new_max_vertices = mesh->max_vertices * 2;
        void *tmp =
            realloc(mesh->vertices, sizeof(fm_vec3_t) * new_max_vertices);
        if (tmp == NULL) {
            return NULL;
        }
        mesh->vertices = tmp;
        mesh->max_vertices = new_max_vertices;
    }
    assert(mesh->n_vertices < mesh->max_vertices);
    if (i_vertex_p != NULL) {
        *i_vertex_p = mesh->n_vertices;
    }
    return &mesh->vertices[mesh->n_vertices++];
}

struct geom_polygon *add_polygon(struct geom_mesh *mesh) {
    if (mesh->n_polygons == mesh->max_polygons) {
        int new_max_polygons = mesh->max_polygons * 2;
        void *tmp = realloc(mesh->polygons,
                            sizeof(struct geom_polygon) * new_max_polygons);
        if (tmp == NULL) {
            return NULL;
        }
        mesh->polygons = tmp;
        mesh->max_polygons = new_max_polygons;
    }
    assert(mesh->n_polygons < mesh->max_polygons);
    return &mesh->polygons[mesh->n_polygons++];
}

struct primitive *geom_mesh_to_primitive(struct geom_mesh *mesh) {
    struct primitive *primitive = malloc(sizeof(struct primitive));
    if (primitive == NULL) {
        return NULL;
    }
    int n_triangles = 0;
    for (int i = 0; i < mesh->n_polygons; i++) {
        if (mesh->polygons[i].n_vertices >= 3) {
            n_triangles += mesh->polygons[i].n_vertices - 2;
        }
    }
    primitive->vertices = malloc(sizeof(struct vertex) * 3 * n_triangles);
    primitive->indices = malloc(sizeof(uint16_t) * 3 * n_triangles);
    if (primitive->vertices == NULL || primitive->indices == NULL) {
        free(primitive->vertices);
        free(primitive->indices);
        free(primitive);
        return NULL;
    }
    primitive->material = -1;
    primitive->index_count = 3 * n_triangles;

    int i_tri = 0;
    for (int i = 0; i < mesh->n_polygons; i++) {
        struct geom_polygon *polygon = &mesh->polygons[i];
        for (int j = 0; j < polygon->n_vertices - 2; j++) {
            int tri_verts[3] = {
                polygon->vertices[0],
                polygon->vertices[j + 1],
                polygon->vertices[j + 2],
            };
            fm_vec3_t a, b, n;
            fm_vec3_sub(&a, &mesh->vertices[tri_verts[1]],
                        &mesh->vertices[tri_verts[0]]);
            fm_vec3_sub(&b, &mesh->vertices[tri_verts[2]],
                        &mesh->vertices[tri_verts[0]]);
            fm_vec3_cross(&n, &a, &b);
            fm_vec3_norm(&n, &n);
            for (int k = 0; k < 3; k++) {
                fm_vec3_t *v = &mesh->vertices[tri_verts[k]];
                int16_t pos[3] = MGFX_POS(v->x, v->y, v->z);
                memcpy(primitive->vertices[i_tri * 3 + k].pos, pos,
                       sizeof(pos));
                primitive->vertices[i_tri * 3 + k].normal =
                    MGFX_NRMF(n.x, n.y, n.z);
                primitive->indices[i_tri * 3 + k] = i_tri * 3 + k;
            }
            i_tri++;
        }
    }
    assert(i_tri == n_triangles);

    return primitive;
}

void generate_tower_geometry_floor(struct geom_mesh *mesh,
                                   int segments_per_floor, int floor,
                                   int corridor) {
    // TODO handle corridor
    float angle = -2 * FM_PI / segments_per_floor / 2;
    fm_vec3_t a, d;
    for (int i = -1; i < segments_per_floor; i++) {
        fm_vec3_t b, c;
        fm_sincosf(angle, &b.y, &b.x);
        b.z = 0.0f + floor;
        c = b;
        c.z = 1.0f + floor;
        if (i != -1) {
            int i_a, i_b, i_c, i_d;
            *add_vertex(mesh, &i_a) = a;
            *add_vertex(mesh, &i_b) = b;
            *add_vertex(mesh, &i_c) = c;
            *add_vertex(mesh, &i_d) = d;
            *add_polygon(mesh) = (struct geom_polygon){
                {i_a, i_b, i_c, i_d},
                4,
            };
        }
        a = b;
        d = c;
        angle += 2 * FM_PI / segments_per_floor;
    }
}

struct primitive *generate_tower_geometry(struct tower *tower) {
    struct geom_mesh *mesh = malloc_mesh(16, 16);
    for (int i = 0; i < tower->n_floors; i++) {
        generate_tower_geometry_floor(mesh, tower->segments_per_floor, i,
                                      tower->floors[i].corridor);
    }
    struct primitive *primitive = geom_mesh_to_primitive(mesh);
    free_mesh(mesh);
    return primitive;
}

void generate_tower_geometry_floor_walls_vertical(struct geom_mesh *mesh,
                                                  int segments_per_floor,
                                                  int floor,
                                                  uint8_t *wall_flags) {
    float angle = -2 * FM_PI / segments_per_floor / 2;
    for (int i = 0; i < segments_per_floor; i++) {
        if (wall_flags[i] & WF_VERTICAL) {
            fm_vec3_t a, b, c, d;
            fm_sincosf(angle, &a.y, &a.x);
            a.z = 0.0f + floor;
            fm_vec3_scale(&d, &a, 1.3f);
            d.z = 0.0f + floor;
            b = a;
            fm_vec3_scale(&c, &b, 1.3f);
            b.z = c.z = 1.0f + floor;
            int i_a, i_b, i_c, i_d;
            *add_vertex(mesh, &i_a) = a;
            *add_vertex(mesh, &i_b) = b;
            *add_vertex(mesh, &i_c) = c;
            *add_vertex(mesh, &i_d) = d;
            *add_polygon(mesh) = (struct geom_polygon){
                {i_a, i_b, i_c, i_d},
                4,
            };
        }
        angle += 2 * FM_PI / segments_per_floor;
    }
}

void generate_tower_geometry_floor_walls_horizontal(struct geom_mesh *mesh,
                                                    int segments_per_floor,
                                                    int floor,
                                                    uint8_t *wall_flags) {
    float angle = -2 * FM_PI / segments_per_floor / 2;
    for (int i = 0; i < segments_per_floor; i++) {
        if (wall_flags[i] & WF_HORIZONTAL) {
            fm_vec3_t a, b, c, d;
            fm_sincosf(angle, &a.y, &a.x);
            fm_sincosf(angle + 2 * FM_PI / segments_per_floor, &b.y, &b.x);
            a.z = 1.0f + floor;
            b.z = 1.0f + floor;
            fm_vec3_scale(&d, &a, 1.3f);
            d.z = 1.0f + floor;
            fm_vec3_scale(&c, &b, 1.3f);
            c.z = 1.0f + floor;
            int i_a, i_b, i_c, i_d;
            *add_vertex(mesh, &i_a) = a;
            *add_vertex(mesh, &i_b) = b;
            *add_vertex(mesh, &i_c) = c;
            *add_vertex(mesh, &i_d) = d;
            *add_polygon(mesh) = (struct geom_polygon){
                {i_a, i_b, i_c, i_d},
                4,
            };
        }
        angle += 2 * FM_PI / segments_per_floor;
    }
}

void generate_tower_geometry_floor_walls(struct geom_mesh *mesh,
                                         int segments_per_floor, int floor,
                                         uint8_t *wall_flags) {
    generate_tower_geometry_floor_walls_vertical(mesh, segments_per_floor,
                                                 floor, wall_flags);
    generate_tower_geometry_floor_walls_horizontal(mesh, segments_per_floor,
                                                   floor, wall_flags);
}

struct primitive *generate_tower_walls_geometry(struct tower *tower) {
    struct geom_mesh *mesh = malloc_mesh(16, 16);
    for (int i = 0; i < tower->n_floors; i++) {
        generate_tower_geometry_floor_walls(mesh, tower->segments_per_floor, i,
                                            tower->floors[i].wall_flags);
    }
    struct primitive *primitive = geom_mesh_to_primitive(mesh);
    free_mesh(mesh);
    return primitive;
}
