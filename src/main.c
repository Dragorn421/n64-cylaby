// SPDX-FileCopyrightText: 2026 Dragorn421
// SPDX-License-Identifier: CC0-1.0

#include <assert.h>
#include <math.h>
#include <stdbool.h>
#include <stdint.h>
#include <stdlib.h>

#include <libdragon.h>

#include "geometry.h"
#include "model.h"
#include "tower.h"

#include "../assets/Suzanne.h"

#ifndef ARRAY_COUNT
#define ARRAY_COUNT(arr) (sizeof(arr) / sizeof(arr[0]))
#endif

struct GfxCtx {
    mgfx_matrices_t ud_mat_buf[5];
    int i_ud_mat_buf;
    struct primitive *used_primitives[2];
    int n_used_primitives;
};

mgfx_matrices_t *build_matrices(struct GfxCtx *gfx_ctx,
                                fm_mat4_t *mat_projection, fm_mat4_t *mat_view,
                                fm_mat4_t *mat_model) {
    fm_mat4_t mat_view_model;
    fm_mat4_mul(&mat_view_model, mat_view, mat_model);
    fm_mat4_t mat_projection_view_model;
    fm_mat4_mul(&mat_projection_view_model, mat_projection, &mat_view_model);
    fm_mat4_t mat_view_model_inv, mat_normal;
    fm_mat4_inverse(&mat_view_model_inv, &mat_view_model);
    fm_mat4_transpose(&mat_normal, &mat_view_model_inv);
    float mat_normal_max = 0.0f;
    for (int i = 0; i < 3; i++) {
        for (int j = 0; j < 3; j++) {
            float f = fabsf(mat_normal.m[i][j]);
            if (f > mat_normal_max) {
                mat_normal_max = f;
            }
        }
    }
    if (mat_normal_max != 0.0f) {
        float f = 1.0f / mat_normal_max;
        fm_mat4_scale(&mat_normal, &(fm_vec3_t){{f, f, f}});
    }
    assert(gfx_ctx->i_ud_mat_buf < ARRAY_COUNT(gfx_ctx->ud_mat_buf));
    mgfx_matrices_t *ud_matrices = &gfx_ctx->ud_mat_buf[gfx_ctx->i_ud_mat_buf];
    gfx_ctx->i_ud_mat_buf++;
    mgfx_get_matrices(ud_matrices, &(mgfx_matrices_parms_t){
                                       mat_projection_view_model.m[0],
                                       mat_view_model.m[0],
                                       mat_normal.m[0],
                                   });
    data_cache_hit_writeback(ud_matrices, sizeof(mgfx_matrices_t));
    return ud_matrices;
}

void add_used_primitive(struct GfxCtx *gfx_ctx, struct primitive *primitive) {
    assert(gfx_ctx->n_used_primitives < ARRAY_COUNT(gfx_ctx->used_primitives));
    gfx_ctx->used_primitives[gfx_ctx->n_used_primitives++] = primitive;
}

void draw_primitive(struct primitive *primitive) {
    mg_bind_vertex_buffer(primitive->vertices);
    mg_draw_indexed(
        &(mg_input_assembly_parms_t){
            MG_PRIMITIVE_TOPOLOGY_TRIANGLE_LIST,
            false,
        },
        primitive->indices, primitive->index_count, 0);
}

struct defered_primitives_free_ctx {
    struct {
        struct primitive *primitive;
        void (*free_func)(struct primitive *);
    } primitives_to_free[6];
    bool has_primitives_to_free;
};

void defered_primitives_free_init_ctx(struct defered_primitives_free_ctx *ctx) {
    for (int i = 0; i < ARRAY_COUNT(ctx->primitives_to_free); i++) {
        ctx->primitives_to_free[i].primitive = NULL;
    }
    ctx->has_primitives_to_free = false;
}

void defered_primitives_free_add(struct defered_primitives_free_ctx *ctx,
                                 struct primitive *primitive,
                                 void (*free_func)(struct primitive *)) {
    bool is_full = true;
    for (int i = 0; i < ARRAY_COUNT(ctx->primitives_to_free); i++) {
        if (ctx->primitives_to_free[i].primitive == NULL) {
            ctx->primitives_to_free[i].primitive = primitive;
            ctx->primitives_to_free[i].free_func = free_func;
            is_full = false;
            break;
        }
    }
    assert(!is_full);
    ctx->has_primitives_to_free = true;
}

void defered_primitives_free_free(struct defered_primitives_free_ctx *ctx,
                                  struct GfxCtx *gfx_ctx_buf, int n_gfx_ctx) {
    if (!ctx->has_primitives_to_free) {
        return;
    }
    bool all_freed = true;
    for (int i = 0; i < ARRAY_COUNT(ctx->primitives_to_free); i++) {
        if (ctx->primitives_to_free[i].primitive != NULL) {
            bool is_primitive_used = false;
            for (int j = 0; j < n_gfx_ctx; j++) {
                struct GfxCtx *gfx_ctx = &gfx_ctx_buf[j];
                for (int k = 0; k < gfx_ctx->n_used_primitives; k++) {
                    if (gfx_ctx->used_primitives[k] ==
                        ctx->primitives_to_free[i].primitive) {
                        is_primitive_used = true;
                    }
                }
            }
            if (is_primitive_used) {
                ctx->primitives_to_free[i].free_func(
                    ctx->primitives_to_free[i].primitive);
                ctx->primitives_to_free[i].primitive = NULL;
            } else {
                all_freed = false;
            }
        }
    }
    ctx->has_primitives_to_free = !all_freed;
}

// like fm_lerp_angle but working properly, for until my fm fixes make it
// upstream
float my_lerp_angle(float a, float b, float t) {
    float diff = fmodf((b - a), FM_PI * 2);
    float dist = fmodf(diff * 2, FM_PI * 2) - diff;
    return a + dist * t;
}

float fm_wrapf(float x, float y) {
    float v = fm_fmodf(x, y);
    if (v < 0)
        v += y;
    return v;
}

int main(void) {
    debug_init_emulog();

    joypad_init();

    display_init(RESOLUTION_320x240, DEPTH_16_BPP, 3, GAMMA_NONE,
                 FILTERS_RESAMPLE_ANTIALIAS_DEDITHER);
    rdpq_init();
    if (0) {
        rdpq_debug_start();
        rdpq_debug_log(true);
    }
    mg_init();

    const uint8_t font_id = 1;
    rdpq_text_register_font(font_id,
                            rdpq_font_load_builtin(FONT_BUILTIN_DEBUG_MONO));

    struct GfxCtx *gfx_ctx_buf =
        malloc(sizeof(struct GfxCtx) * display_get_num_buffers());
    int i_gfx_ctx = 0;

    mg_vertex_attribute_t vertex_attributes[] = {
        {
            .input = MGFX_ATTRIBUTE_POSITION,
            .offset = offsetof(struct vertex, pos),
        },
        {
            .input = MGFX_ATTRIBUTE_NORMAL,
            .offset = offsetof(struct vertex, normal),
        },
    };

    mg_pipeline_t *pipeline = mg_pipeline_create(&(mg_pipeline_parms_t){
        mgfx_get_shader_ucode(0),
        .vertex_layout.attribute_count =
            sizeof(vertex_attributes) / sizeof(vertex_attributes[0]),
        .vertex_layout.attributes = vertex_attributes,
        .vertex_layout.stride = sizeof(struct vertex),
    });

    const mg_uniform_t *u_fog =
        mg_pipeline_get_uniform(pipeline, MGFX_BINDING_FOG);
    const mg_uniform_t *u_lighting =
        mg_pipeline_get_uniform(pipeline, MGFX_BINDING_LIGHTING);
    const mg_uniform_t *u_matrices =
        mg_pipeline_get_uniform(pipeline, MGFX_BINDING_MATRICES);

    mgfx_fog_t ud_fog;
    mgfx_get_fog(&ud_fog, &(mgfx_fog_parms_t){0});

    mgfx_lighting_t ud_lighting;
    mgfx_get_lighting(&ud_lighting, &(mgfx_lighting_parms_t){
                                        (color_t){100, 100, 100, 255},
                                        (mgfx_light_parms_t[]){
                                            {
                                                {{1, 0, -1, 0}},
                                                {100, 100, 100, 0},
                                            },
                                        },
                                        1,
                                    });

    fm_mat4_t mat_projection;
    mg_mat4_perspective(&mat_projection, FM_DEG2RAD(60),
                        (float)display_get_width() / display_get_height(), 0.1f,
                        10.0f);

    float suzanne_angle = 0.0f;
    float suzanne_height = 0.0f;
    float camera_angle = 0.0f;
    float camera_eye_height = 0.0f;
    float camera_at_height = 0.0f;

    struct defered_primitives_free_ctx defered_primitives_free_ctx;
    defered_primitives_free_init_ctx(&defered_primitives_free_ctx);

    unsigned int seed;
    struct tower *tower = NULL;
    struct primitive *tower_primitive = NULL;
    struct primitive *tower_walls_primitive = NULL;
    bool rebuild_tower = true;

    while (true) {
        struct GfxCtx *gfx_ctx = &gfx_ctx_buf[i_gfx_ctx];
        i_gfx_ctx++;
        i_gfx_ctx %= display_get_num_buffers();
        gfx_ctx->i_ud_mat_buf = 0;
        gfx_ctx->n_used_primitives = 0;

        if (rebuild_tower) {
            if (tower != NULL) {
                free_tower(tower);
                tower = NULL;
            }
            if (tower_primitive != NULL) {
                defered_primitives_free_add(&defered_primitives_free_ctx,
                                            tower_primitive,
                                            geom_mesh_free_primitive);
                tower_primitive = NULL;
            }
            if (tower_walls_primitive != NULL) {
                defered_primitives_free_add(&defered_primitives_free_ctx,
                                            tower_walls_primitive,
                                            geom_mesh_free_primitive);
                tower_walls_primitive = NULL;
            }

            tower =
                malloc_tower(5, 10, WF_VERTICAL_UNSET | WF_HORIZONTAL_UNSET);
            assert(tower != NULL);

            seed = (unsigned int)getentropy32() + (unsigned int)get_ticks();
            srand(seed);

            int n_corridors = 1 + rand() % 2;
            if (rand() % 3 == 0) {
                n_corridors += 1;
            }
            for (int i = 0; i < n_corridors; i++) {
                tower->floors[rand() % tower->n_floors].corridor =
                    rand() % (tower->segments_per_floor / 2);
            }

            build_tower_walls(tower);

            tower_primitive = generate_tower_geometry(tower);
            assert(tower_primitive != NULL);
            tower_walls_primitive = generate_tower_walls_geometry(tower);
            assert(tower_walls_primitive != NULL);

            data_cache_writeback_invalidate_all();

            rebuild_tower = false;
        }

        surface_t *surf = display_get();
        uint64_t t = get_ticks_ms();
        float dt = display_get_delta_time();
        joypad_poll();
        joypad_inputs_t inputs = joypad_get_inputs(JOYPAD_PORT_1);
        joypad_buttons_t pressed = joypad_get_buttons_pressed(JOYPAD_PORT_1);

        suzanne_angle += inputs.stick_x / 60.0f * FM_DEG2RAD(100) * dt;
        suzanne_angle = fm_wrap_angle(suzanne_angle);
        {
            int segment =
                (int)fm_roundf(suzanne_angle /
                               (2 * FM_PI / tower->segments_per_floor)) %
                tower->segments_per_floor;
            int floor = (int)fm_roundf(suzanne_height);
            if (floor >= 0 && floor < tower->n_floors) {
                if (tower->floors[floor].wall_flags[segment] & WF_VERTICAL) {
                    float limit = (segment * 2 * FM_PI - FM_DEG2RAD(50)) /
                                  tower->segments_per_floor;
                    if (fm_wrap_angle(suzanne_angle - limit) >= FM_PI) {
                        suzanne_angle = limit;
                    }
                }
                if (tower->floors[floor]
                        .wall_flags[(segment + 1) % tower->segments_per_floor] &
                    WF_VERTICAL) {
                    float limit = (segment * 2 * FM_PI + FM_DEG2RAD(50)) /
                                  tower->segments_per_floor;
                    if (fm_wrap_angle(suzanne_angle - limit) < FM_PI) {
                        suzanne_angle = limit;
                    }
                }
            }
        }
        if (abs(inputs.stick_x) < 10) {
            float target_suzanne_angle =
                fm_roundf(suzanne_angle /
                          (2 * FM_PI / tower->segments_per_floor)) *
                (2 * FM_PI / tower->segments_per_floor);
            suzanne_angle = my_lerp_angle(suzanne_angle, target_suzanne_angle,
                                          0.1f * 60 * dt);
        }

        if (pressed.a) {
            int floor = (int)fm_roundf(suzanne_height);
            if (floor >= 0 && floor < tower->n_floors) {
                int corridor = tower->floors[floor].corridor;
                if (corridor != -1) {
                    int segment = (int)fm_roundf(
                                      suzanne_angle /
                                      (2 * FM_PI / tower->segments_per_floor)) %
                                  tower->segments_per_floor;
                    if (corridor == segment ||
                        corridor + tower->segments_per_floor / 2 == segment) {
                        suzanne_angle += FM_PI;
                    }
                }
            }
        }

        if (pressed.z) {
            rebuild_tower = true;
        }

        suzanne_height += inputs.stick_y / 60.0f * dt;
        if (suzanne_height < -0.3f) {
            suzanne_height = -0.3f;
        }
        if (suzanne_height > tower->n_floors - 1 + 0.3f) {
            suzanne_height = tower->n_floors - 1 + 0.3f;
        }
        {
            int segment =
                (int)fm_roundf(suzanne_angle /
                               (2 * FM_PI / tower->segments_per_floor)) %
                tower->segments_per_floor;
            int floor = (int)fm_roundf(suzanne_height);
            if (floor >= 0 && floor < tower->n_floors) {
                if (tower->floors[floor].wall_flags[segment] & WF_HORIZONTAL) {
                    float limit = floor + 0.3f;
                    if (suzanne_height > limit) {
                        suzanne_height = limit;
                    }
                }
                if (floor > 0 && (tower->floors[floor - 1].wall_flags[segment] &
                                  WF_HORIZONTAL)) {
                    float limit = floor - 0.3f;
                    if (suzanne_height < limit) {
                        suzanne_height = limit;
                    }
                }
            }
        }
        if (abs(inputs.stick_y) < 10) {
            float target_suzanne_height = fm_roundf(suzanne_height);
            suzanne_height =
                fm_lerp(suzanne_height, target_suzanne_height, 0.1f * 60 * dt);
        }

        camera_angle =
            my_lerp_angle(camera_angle, suzanne_angle, 0.1f * 60 * dt);
        camera_at_height =
            my_lerp_angle(camera_at_height, suzanne_height, 0.2f * 60 * dt);
        camera_eye_height =
            my_lerp_angle(camera_eye_height, camera_at_height, 0.1f * 60 * dt);

        fm_vec3_t eye;
        fm_sincosf(camera_angle, &eye.y, &eye.x);
        eye.z = 0.0f;
        fm_vec3_scale(&eye, &eye, 2.0f);
        eye.z = camera_eye_height;
        fm_vec3_t target = {{0, 0, camera_at_height}};
        fm_mat4_t mat_view;
        fm_mat4_lookat(&mat_view, &eye, &target, &(fm_vec3_t){{0, 0, 1}});

        rdpq_attach(surf, display_get_zbuf());

        rdpq_clear((color_t){
            100,
            200,
            80 + 20 * fm_cosf(t * 2 * FM_PI / 5000),
            255,
        });
        rdpq_clear_z(ZBUF_MAX);

        rdpq_mode_begin();
        rdpq_set_mode_standard();
        rdpq_mode_dithering(DITHER_SQUARE_SQUARE);
        rdpq_mode_antialias(AA_STANDARD);
        rdpq_mode_combiner(RDPQ_COMBINER1((PRIM, 0, SHADE, 0), (0, 0, 0, 1)));
        rdpq_mode_zbuf(true, true);
        rdpq_mode_end();

        rdpq_set_prim_color((color_t){255, 100, 100, 255});

        mg_pipeline_bind(pipeline);

        mg_set_viewport(&(mg_viewport_t){
            .x = 0,
            .y = 0,
            .width = display_get_width(),
            .height = display_get_height(),
            .minDepth = 0.0f,
            .maxDepth = 1.0f,
        });

        mg_set_culling(&(mg_culling_parms_t){.cull_mode = MG_CULL_MODE_BACK});

        mg_set_geometry_flags(MG_GEOMETRY_FLAGS_SHADE_ENABLED |
                              MG_GEOMETRY_FLAGS_Z_ENABLED);

        mg_uniform_load(u_fog, &ud_fog);
        mg_uniform_load(u_lighting, &ud_lighting);

        {
            fm_mat4_t mat_model;
            fm_mat4_identity(&mat_model);
            fm_vec3_t translate = {{0.0f, -1.1f, suzanne_height}};
            fm_mat4_translate(&mat_model, &translate);
            fm_quat_t rotation;
            fm_quat_from_euler_zyx(&rotation, 0.0f, 0.0f,
                                   suzanne_angle + FM_PI / 2);
            fm_mat4_rotate(&mat_model, &rotation);
            fm_mat4_scale(&mat_model, &(fm_vec3_t){{0.2f, 0.2f, 0.2f}});
            mgfx_matrices_t *ud_matrices =
                build_matrices(gfx_ctx, &mat_projection, &mat_view, &mat_model);

            mg_uniform_load(u_matrices, ud_matrices);

            draw_primitive(&Suzanne_0);
        }

        rdpq_set_prim_color((color_t){100, 100, 255, 255});
        {
            fm_mat4_t mat_model;
            fm_mat4_identity(&mat_model);
            fm_mat4_translate(&mat_model, &(fm_vec3_t){{0.0f, 0.0f, -0.5f}});
            mgfx_matrices_t *ud_matrices =
                build_matrices(gfx_ctx, &mat_projection, &mat_view, &mat_model);

            mg_uniform_load(u_matrices, ud_matrices);

            add_used_primitive(gfx_ctx, tower_primitive);
            draw_primitive(tower_primitive);
        }

        mg_set_culling(&(mg_culling_parms_t){.cull_mode = MG_CULL_MODE_NONE});
        rdpq_set_prim_color((color_t){50, 50, 200, 255});
        {
            fm_mat4_t mat_model;
            fm_mat4_identity(&mat_model);
            fm_mat4_translate(&mat_model, &(fm_vec3_t){{0.0f, 0.0f, -0.5f}});
            mgfx_matrices_t *ud_matrices =
                build_matrices(gfx_ctx, &mat_projection, &mat_view, &mat_model);

            mg_uniform_load(u_matrices, ud_matrices);

            add_used_primitive(gfx_ctx, tower_walls_primitive);
            draw_primitive(tower_walls_primitive);
        }

        rdpq_set_mode_standard();

        rdpq_text_printf(&(rdpq_textparms_t){}, font_id, 10, 10, "0x%08X",
                         seed);

        rdpq_detach_show();

        defered_primitives_free_free(&defered_primitives_free_ctx, gfx_ctx_buf,
                                     display_get_num_buffers());
    }
}
