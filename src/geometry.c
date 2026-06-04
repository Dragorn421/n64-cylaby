// SPDX-FileCopyrightText: 2026 Dragorn421
// SPDX-License-Identifier: CC0-1.0

#include <stdbool.h>
#include <stdint.h>
#include <stdlib.h>
#include <string.h>

#include <libdragon.h>

#include "geometry.h"
#include "model.h"
#include "polygonal_collision.h"
#include "tower.h"

/*
 * Basic runtime geometry helpers.
 */

struct geom_polygon {
#define POLY_MAX_VERTS 4
    int vertices[POLY_MAX_VERTS];
    int n_vertices;
};

struct geom_mesh {
    /**
     * Buffer of vertices. There are n_vertices vertices. The buffer can hold up
     * to max_vertices. The add_vertex function automatically grows the buffer.
     */
    fm_vec3_t *vertices;
    /**
     * Buffer of texture coordinates. Same length, buffer size and indices as
     * vertices. May be NULL.
     */
    float (*st_attribute)[2];
    /**
     * Buffer of polygons. There are n_polygons vertices. The buffer can hold up
     * to max_polygons. The add_polygon function automatically grows the buffer.
     */
    struct geom_polygon *polygons;
    int max_vertices;
    int n_vertices;
    int max_polygons;
    int n_polygons;
};

struct geom_mesh *malloc_mesh(int max_vertices_ini, int max_polygons_ini,
                              bool with_st_attribute) {
    struct geom_mesh *mesh = malloc(sizeof(struct geom_mesh));
    if (mesh == NULL) {
        return NULL;
    }
    mesh->vertices = malloc(sizeof(fm_vec3_t) * max_vertices_ini);
    if (with_st_attribute) {
        mesh->st_attribute = malloc(sizeof(float[2]) * max_vertices_ini);
    } else {
        mesh->st_attribute = NULL;
    }
    mesh->polygons = malloc(sizeof(struct geom_polygon) * max_polygons_ini);
    if (mesh->vertices == NULL ||
        (with_st_attribute && mesh->st_attribute == NULL) ||
        mesh->polygons == NULL) {
        free(mesh->vertices);
        free(mesh->st_attribute);
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

/**
 * Add a vertex to a mesh, returning a pointer to its (uninitialized) data (or
 * NULL on failure), and setting i_vertex_p to the corresponding vertex index.
 */
fm_vec3_t *add_vertex(struct geom_mesh *mesh, int *i_vertex_p) {
    if (mesh->n_vertices == mesh->max_vertices) {
        int new_max_vertices = mesh->max_vertices * 2;
        void *tmp =
            realloc(mesh->vertices, sizeof(fm_vec3_t) * new_max_vertices);
        if (tmp == NULL) {
            return NULL;
        }
        mesh->vertices = tmp;
        if (mesh->st_attribute != NULL) {
            void *tmp2 = realloc(mesh->st_attribute,
                                 sizeof(float[2]) * new_max_vertices);
            if (tmp2 == NULL) {
                return NULL;
            }
            mesh->st_attribute = tmp2;
        }
        mesh->max_vertices = new_max_vertices;
    }
    assert(mesh->n_vertices < mesh->max_vertices);
    if (i_vertex_p != NULL) {
        *i_vertex_p = mesh->n_vertices;
    }
    return &mesh->vertices[mesh->n_vertices++];
}

/**
 * Add a polygon to a mesh, returning a pointer to its (uninitialized) data (or
 * NULL on failure).
 */
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

/**
 * Helper for adding a quad to a mesh, given four vertices.
 * Returns true on success.
 */
bool add_quad(struct geom_mesh *mesh, fm_vec3_t *a, fm_vec3_t *b, fm_vec3_t *c,
              fm_vec3_t *d) {
    int i_a, i_b, i_c, i_d;
    fm_vec3_t *v_a, *v_b, *v_c, *v_d;
    v_a = add_vertex(mesh, &i_a);
    if (v_a == NULL) {
        return false;
    }
    *v_a = *a;
    v_b = add_vertex(mesh, &i_b);
    if (v_b == NULL) {
        return false;
    }
    *v_b = *b;
    v_c = add_vertex(mesh, &i_c);
    if (v_c == NULL) {
        return false;
    }
    *v_c = *c;
    v_d = add_vertex(mesh, &i_d);
    if (v_d == NULL) {
        return false;
    }
    *v_d = *d;
    struct geom_polygon *polygon = add_polygon(mesh);
    if (polygon == NULL) {
        return false;
    }
    *polygon = (struct geom_polygon){
        {i_a, i_b, i_c, i_d},
        4,
    };
    return true;
}

/**
 * Convert a mesh to a (dynamically allocated) primitive (data that can be
 * drawn).
 */
struct primitive *geom_mesh_to_primitive_impl(
    struct geom_mesh *mesh, float scale, enum vertex_kind vertices_kind,
    size_t vertex_sz,
    void (*vertex_handler)(struct geom_mesh *, int vertex_i,
                           int16_t *vertex_pos, uint16_t vertex_normal,
                           void *vertex)) {
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
    primitive->vertices_kind = vertices_kind;
    char *primitive_vertices_buf = malloc(vertex_sz * 3 * n_triangles);
    primitive->vertices = primitive_vertices_buf;
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
                int16_t pos[3] = {
                    fm_roundf(v->x * scale),
                    fm_roundf(v->y * scale),
                    fm_roundf(v->z * scale),
                };
                vertex_handler(mesh, tri_verts[k], pos,
                               MGFX_NRMF(n.x, n.y, n.z),
                               primitive_vertices_buf);
                primitive_vertices_buf += vertex_sz;
                primitive->indices[i_tri * 3 + k] = i_tri * 3 + k;
            }
            i_tri++;
        }
    }
    assert(i_tri == n_triangles);

    return primitive;
}

void vertex_handler_standard(struct geom_mesh *mesh, int vertex_i,
                             int16_t *vertex_pos, uint16_t vertex_normal,
                             void *vertex) {
    struct vertex *v = vertex;
    memcpy(v->pos, vertex_pos, sizeof(v->pos));
    v->normal = vertex_normal;
}

struct primitive *geom_mesh_to_primitive(struct geom_mesh *mesh, float scale) {
    return geom_mesh_to_primitive_impl(mesh, scale, VERTEX_KIND_STANDARD,
                                       sizeof(struct vertex),
                                       vertex_handler_standard);
}

void vertex_handler_textured(struct geom_mesh *mesh, int vertex_i,
                             int16_t *vertex_pos, uint16_t vertex_normal,
                             void *vertex) {
    struct textured_vertex *v = vertex;
    memcpy(v->pos, vertex_pos, sizeof(v->pos));
    if (mesh->st_attribute != NULL) {
        memcpy(v->st,
               &(int16_t[2])MGFX_TEX(mesh->st_attribute[vertex_i][0],
                                     mesh->st_attribute[vertex_i][1]),
               sizeof(v->st));
    } else {
        v->st[0] = v->st[1] = 0;
    }
    v->normal = vertex_normal;
}

struct primitive *geom_mesh_to_primitive_textured(struct geom_mesh *mesh,
                                                  float scale) {
    return geom_mesh_to_primitive_impl(mesh, scale, VERTEX_KIND_TEXTURED,
                                       sizeof(struct textured_vertex),
                                       vertex_handler_textured);
}

void geom_mesh_free_primitive(struct primitive *primitive) {
    free(primitive->vertices);
    free(primitive->indices);
    free(primitive);
}

struct polycol_mesh *geom_mesh_to_polycol_mesh(struct geom_mesh *mesh,
                                               float scale) {
    int n_triangles = 0;
    for (int i = 0; i < mesh->n_polygons; i++) {
        if (mesh->polygons[i].n_vertices >= 3) {
            n_triangles += mesh->polygons[i].n_vertices - 2;
        }
    }
    fm_vec3_t(*tris)[3] = malloc(sizeof(fm_vec3_t[3]) * n_triangles);
    if (tris == NULL) {
        return NULL;
    }
    int i_tri = 0;
    for (int i = 0; i < mesh->n_polygons; i++) {
        struct geom_polygon *polygon = &mesh->polygons[i];
        for (int j = 0; j < polygon->n_vertices - 2; j++) {
            int tri_verts[3] = {
                polygon->vertices[0],
                polygon->vertices[j + 1],
                polygon->vertices[j + 2],
            };
            for (int k = 0; k < 3; k++) {
                tris[i_tri][k] = mesh->vertices[tri_verts[k]];
            }
            i_tri++;
        }
    }
    struct polycol_mesh *polycol_mesh = polycol_mesh_new(tris, n_triangles);
    free(tris);
    return polycol_mesh;
}

void generate_tower_geometry_floor(struct geom_mesh *mesh,
                                   int segments_per_floor, int floor,
                                   int corridor) {
    float angle = -2 * FM_PI / segments_per_floor / 2;
    fm_vec3_t a, d;
    struct {
        fm_vec3_t a2, b2, c2, d2;
    } corridor_verts[2];
    int n_corridor_verts = 0;
    for (int i = -1; i < segments_per_floor; i++) {
        fm_vec3_t b, c;
        fm_sincosf(angle, &b.y, &b.x);
        b.z = 0.0f + floor;
        c = b;
        c.z = 1.0f + floor;
        if (i != -1) {
            if (corridor != -1 &&
                (i == corridor || i == corridor + segments_per_floor / 2)) {
                fm_vec3_t fc; // face center
                fm_vec3_lerp(&fc, &a, &c, 0.5f);
                fm_vec3_t a2, b2, c2, d2;
                fm_vec3_lerp(&a2, &a, &fc, 0.2f);
                fm_vec3_lerp(&b2, &b, &fc, 0.2f);
                fm_vec3_lerp(&c2, &c, &fc, 0.2f);
                fm_vec3_lerp(&d2, &d, &fc, 0.2f);
                add_quad(mesh, &a, &b, &b2, &a2);
                add_quad(mesh, &b, &c, &c2, &b2);
                add_quad(mesh, &c, &d, &d2, &c2);
                add_quad(mesh, &d, &a, &a2, &d2);
                corridor_verts[n_corridor_verts].a2 = a2;
                corridor_verts[n_corridor_verts].b2 = b2;
                corridor_verts[n_corridor_verts].c2 = c2;
                corridor_verts[n_corridor_verts].d2 = d2;
                n_corridor_verts++;
            } else {
                add_quad(mesh, &a, &b, &c, &d);
            }
        }
        a = b;
        d = c;
        angle += 2 * FM_PI / segments_per_floor;
    }
    if (corridor != -1) {
        assert(n_corridor_verts == 2);
        fm_vec3_t *a1 = &corridor_verts[0].a2, *b1 = &corridor_verts[0].b2,
                  *c1 = &corridor_verts[0].c2, *d1 = &corridor_verts[0].d2;
        fm_vec3_t *a2 = &corridor_verts[1].a2, *b2 = &corridor_verts[1].b2,
                  *c2 = &corridor_verts[1].c2, *d2 = &corridor_verts[1].d2;
        add_quad(mesh, a1, b1, a2, b2);
        add_quad(mesh, b1, c1, d2, a2);
        add_quad(mesh, c1, d1, c2, d2);
        add_quad(mesh, d1, a1, b2, c2);
    }
}

struct primitive *generate_tower_geometry(struct tower *tower, float scale) {
    struct geom_mesh *mesh = malloc_mesh(16, 16, false);
    for (int i = 0; i < tower->n_floors; i++) {
        generate_tower_geometry_floor(mesh, tower->segments_per_floor, i,
                                      tower->floors[i].corridor);
    }
    struct primitive *primitive = geom_mesh_to_primitive(mesh, scale);
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

struct primitive *generate_tower_walls_geometry(struct tower *tower,
                                                float scale) {
    struct geom_mesh *mesh = malloc_mesh(16, 16, false);
    for (int i = 0; i < tower->n_floors; i++) {
        generate_tower_geometry_floor_walls(mesh, tower->segments_per_floor, i,
                                            tower->floors[i].wall_flags);
    }
    struct primitive *primitive = geom_mesh_to_primitive(mesh, scale);
    free_mesh(mesh);
    return primitive;
}

struct ground_geometry_res
generate_ground_geometry(fm_vec2_t *from, fm_vec2_t *to, float z,
                         fm_vec2_t *st_from, fm_vec2_t *st_to, int subdivX,
                         int subdivY, fm_vec3_t *noise) {
    struct geom_mesh *mesh = malloc_mesh(16, 16, true);
    fm_vec3_t *noise_map_rough =
        malloc(sizeof(fm_vec3_t) * (subdivX + 1) * (subdivY + 1));
    fm_vec3_t *noise_map =
        malloc(sizeof(fm_vec3_t) * (subdivX + 1) * (subdivY + 1));
    int *vertices = malloc(sizeof(int) * (subdivX + 1) * (subdivY + 1));
    if (mesh == NULL || noise_map_rough == NULL || noise_map == NULL ||
        vertices == NULL) {
        free_mesh(mesh);
        free(noise_map_rough);
        free(noise_map);
        free(vertices);
        return (struct ground_geometry_res){NULL, NULL};
    }
    for (int j = 0; j <= subdivY; j++) {
        for (int i = 0; i <= subdivX; i++) {
            fm_vec3_t *v = &noise_map_rough[j * (subdivX + 1) + i];
            for (int k = 0; k < 3; k++) {
                v->v[k] = noise->v[k] * ((rand() % 20001) / 10000.0f - 1.0f);
            }
        }
    }
#define KERNELW 5
#define KERNELH 5
    // gaussian kernel, sigma=1
    float kernel[KERNELH][KERNELW] = {
        {0.00296902, 0.01330621, 0.02193823, 0.01330621, 0.00296902},
        {0.01330621, 0.0596343, 0.09832033, 0.0596343, 0.01330621},
        {0.02193823, 0.09832033, 0.16210282, 0.09832033, 0.02193823},
        {0.01330621, 0.0596343, 0.09832033, 0.0596343, 0.01330621},
        {0.00296902, 0.01330621, 0.02193823, 0.01330621, 0.00296902},
    };
    for (int j = 0; j <= subdivY; j++) {
        for (int i = 0; i <= subdivX; i++) {
            fm_vec3_t v = {0};
            float fac = 0.0f;
            for (int kj = 0; kj < KERNELH; kj++) {
                for (int ki = 0; ki < KERNELW; ki++) {
                    int ii = i + ki - KERNELW / 2 - 1;
                    int ij = j + kj - KERNELH / 2 - 1;
                    if (ii >= 0 && ii <= subdivX && ij >= 0 && ij <= subdivY) {
                        fm_vec3_t tmp =
                            noise_map_rough[ij * (subdivX + 1) + ii];
                        fm_vec3_scale(&tmp, &tmp, kernel[kj][ki]);
                        fm_vec3_add(&v, &v, &tmp);
                        fac += kernel[kj][ki];
                    }
                }
            }
            fm_vec3_scale(&v, &v, 1.0f / fac);
            noise_map[j * (subdivX + 1) + i] = v;
        }
    }
    for (int j = 0; j <= subdivY; j++) {
        for (int i = 0; i <= subdivX; i++) {
            int k = j * (subdivX + 1) + i;
            *add_vertex(mesh, &vertices[k]) = (fm_vec3_t){{
                fm_lerp(from->x, to->x, (float)i / subdivX) + noise_map[k].x,
                fm_lerp(from->y, to->y, (float)j / subdivY) + noise_map[k].y,
                z + noise_map[k].z,
            }};
            float *st = mesh->st_attribute[vertices[k]];
            st[0] = fm_lerp(st_from->x, st_to->x, (float)i / subdivX);
            st[1] = fm_lerp(st_from->y, st_to->y, (float)j / subdivY);
        }
    }
    for (int j = 0; j < subdivY; j++) {
        for (int i = 0; i < subdivX; i++) {
            struct geom_polygon *poly = add_polygon(mesh);
            poly->n_vertices = 4;
            poly->vertices[0] = vertices[j * (subdivX + 1) + i];
            poly->vertices[1] = vertices[j * (subdivX + 1) + i + 1];
            poly->vertices[2] = vertices[(j + 1) * (subdivX + 1) + i + 1];
            poly->vertices[3] = vertices[(j + 1) * (subdivX + 1) + i];
        }
    }
    struct primitive *primitive = geom_mesh_to_primitive_textured(mesh, 1);
    struct polycol_mesh *polycol_mesh = geom_mesh_to_polycol_mesh(mesh, 1);
    free_mesh(mesh);
    free(noise_map_rough);
    free(noise_map);
    free(vertices);
    return (struct ground_geometry_res){
        primitive,
        polycol_mesh,
    };
}
