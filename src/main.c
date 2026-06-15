// SPDX-FileCopyrightText: 2026 Dragorn421
// SPDX-License-Identifier: CC0-1.0

#include <assert.h>
#include <float.h>
#include <math.h>
#include <stdbool.h>
#include <stdint.h>
#include <stdlib.h>

#include <libdragon.h>

#include "geometry.h"
#include "model.h"
#include "polygonal_collision.h"
#include "texture_gen.h"
#include "tower.h"

#include "../assets/Suzanne.h"

// height of Suzanne's scalp above its origin, in meters
#define SUZANNE_SCALP_Z_M 0.15f
// height of Suzanne's chin below its origin, in meters
#define SUZANNE_CHIN_Z_M 0.25f
// approximate radius of bounding sphere for Suzanne, in centimeters (units)
#define SUZANNE_RADIUS 25.0f

#define TOWER_RADIUS 100.0f

#ifndef ARRAY_COUNT
#define ARRAY_COUNT(arr) (sizeof(arr) / sizeof(arr[0]))
#endif

/**
 * The GfxCtx struct holds data relevant to each frame, for example to achieve
 * N-buffering of uniform data.
 */
struct GfxCtx {
    /**
     * Matrices uniform data buffer.
     * This buffer holds matrices uniforms generated every frame.
     * The next free index in the buffer is stored in i_ud_mat_buf.
     */
    mgfx_matrices_t ud_mat_buf[50];
    int i_ud_mat_buf;
};

/**
 * Given the projection, view and model matrices, compute the relevant matrices
 * and convert them to uniform data for the mgfx vertex shader.
 * The uniform data is stored inside the given gfx_ctx. The function asserts if
 * the uniform data buffer is full.
 */
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

void draw_primitive(struct primitive *primitive) {
    mg_bind_vertex_buffer(primitive->vertices);
    mg_draw_indexed(
        &(mg_input_assembly_parms_t){
            MG_PRIMITIVE_TOPOLOGY_TRIANGLE_LIST,
            false,
        },
        primitive->indices, primitive->index_count, 0);
}

void rspq_block_free_voidp(void *block) { rspq_block_free(block); }

float my_lerp_angle_maxed(float a, float b, float t, float max_step) {
    float diff = fmodf((b - a), FM_PI * 2);
    float dist = fmodf(diff * 2, FM_PI * 2) - diff;
    float step = dist * t;
    if (step < -max_step) {
        step = -max_step;
    }
    if (step > max_step) {
        step = max_step;
    }
    return a + step;
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

    data_cache_writeback_invalidate_all();

    mg_vertex_attribute_t textured_vertex_attributes[] = {
        {
            .input = MGFX_ATTRIBUTE_POSITION,
            .offset = offsetof(struct textured_vertex, pos),
        },
        {
            .input = MGFX_ATTRIBUTE_NORMAL,
            .offset = offsetof(struct textured_vertex, normal),
        },
        {
            .input = MGFX_ATTRIBUTE_TEXCOORD,
            .offset = offsetof(struct textured_vertex, st),
        },
    };

    mg_pipeline_t *textured_pipeline = mg_pipeline_create(&(
        mg_pipeline_parms_t){
        mgfx_get_shader_ucode(0),
        .vertex_layout.attribute_count = sizeof(textured_vertex_attributes) /
                                         sizeof(textured_vertex_attributes[0]),
        .vertex_layout.attributes = textured_vertex_attributes,
        .vertex_layout.stride = sizeof(struct textured_vertex),
    });

    const mg_uniform_t *textured_u_fog =
        mg_pipeline_get_uniform(textured_pipeline, MGFX_BINDING_FOG);
    const mg_uniform_t *textured_u_texturing =
        mg_pipeline_get_uniform(textured_pipeline, MGFX_BINDING_TEXTURING);
    const mg_uniform_t *textured_u_lighting =
        mg_pipeline_get_uniform(textured_pipeline, MGFX_BINDING_LIGHTING);
    const mg_uniform_t *textured_u_matrices =
        mg_pipeline_get_uniform(textured_pipeline, MGFX_BINDING_MATRICES);

    mgfx_fog_t textured_ud_fog;
    mgfx_get_fog(&textured_ud_fog, &(mgfx_fog_parms_t){0});

    mgfx_texturing_t textured_ud_texturing;
    mgfx_get_texturing(&textured_ud_texturing, &(mgfx_texturing_parms_t){
                                                   {1, 1},
                                                   {0, 0},
                                               });

    data_cache_writeback_invalidate_all();

    fm_mat4_t mat_projection;
#define Z_NEAR 30.0f
#define Z_FAR 2000.0f
    mg_mat4_perspective(&mat_projection, FM_DEG2RAD(60),
                        (float)display_get_width() / display_get_height(),
                        Z_NEAR, Z_FAR);

    struct {
        fm_vec3_t suzanne_pos;
        float suzanne_yaw;
        float suzanne_yaw_idle_time;
        float camera_yaw;
    } free_roam_ctx = {0};

    struct {
        // "_angle" refer to angular positions (in radians)
        // "_height" values are in units of floors
        float suzanne_angle;
        float suzanne_height;
        float camera_angle;
        float camera_eye_height;
        float camera_at_height;
    } tower_climb_ctx = {0};

    bool is_climbing_tower = false;
    float cam_switch_timer = 0.0f, cam_switch_timer_ini = 1.0f;
    fm_vec3_t cam_eye, cam_target, cam_up;

    struct {
        unsigned int seed;
        struct tower *tower;
        struct primitive *tower_primitive;
        struct primitive *tower_walls_primitive;
        rspq_block_t *tower_block;
        rspq_block_t *tower_walls_block;
        struct generate_brick_texture_res tex;
        bool rebuild_tower;
        fm_vec3_t pos;
        struct {
            struct {
                int floor;
                int segment;
            } *positions;
            int max_positions;
            int n_positions;
            struct primitive *thread_primitive;
            rspq_block_t *thread_block;
        } ariadnes_thread;
    } towers[3] = {0};
    towers[0].pos = (fm_vec3_t){{300, 0, 0}};
    towers[1].pos = (fm_vec3_t){{-300, 0, 0}};
    towers[2].pos = (fm_vec3_t){{0, 300, 0}};
    for (int i = 0; i < ARRAY_COUNT(towers); i++) {
        towers[i].rebuild_tower = true;

        towers[i].ariadnes_thread.max_positions = 16;
        towers[i].ariadnes_thread.positions =
            malloc(sizeof(*towers[i].ariadnes_thread.positions) *
                   towers[i].ariadnes_thread.max_positions);
        towers[i].ariadnes_thread.n_positions = 0;
    }
    int i_cur_tower = 0;

    rspq_block_begin();
    draw_primitive(&Suzanne_0);
    rspq_block_t *suzanne_block = rspq_block_end();

#define GROUND_EXTENT 1000
#define GROUND_ST_SHIFT 4
    struct ground_geometry_res ground_geometry_res = generate_ground_geometry(
        &(fm_vec2_t){{-GROUND_EXTENT, -GROUND_EXTENT}},
        &(fm_vec2_t){{GROUND_EXTENT, GROUND_EXTENT}}, 0, &(fm_vec2_t){{0, 0}},
        &(fm_vec2_t){{32 << GROUND_ST_SHIFT, 32 << GROUND_ST_SHIFT}}, 10, 10,
        &(fm_vec3_t){{200, 200, 200}});
    struct primitive *ground_primitive = ground_geometry_res.primitive;
    struct polycol_mesh *ground_polycol = ground_geometry_res.polycol_mesh;

    bool success;

    struct generate_ground_texture_res ground_tex_res;
    success = generate_ground_texture(&ground_tex_res);
    assert(success);

    struct primitive *flower_primitive = generate_flower_geometry(50.0f);

    data_cache_writeback_invalidate_all();

    // snap tower positions to ground
    struct {
        float r, a;
    } tower_ground_check_locs[] = {
        {0.0f, 0.0f},
        {TOWER_RADIUS, 0.0f},
        {TOWER_RADIUS, FM_PI / 2},
        {TOWER_RADIUS, FM_PI},
        {TOWER_RADIUS, FM_PI * 3 / 2},
    };
    for (int i = 0; i < ARRAY_COUNT(towers); i++) {
        float z = -FLT_MAX;
        for (int j = 0; j < ARRAY_COUNT(tower_ground_check_locs); j++) {
            fm_vec3_t pos;
            fm_sincosf(tower_ground_check_locs[j].a, &pos.y, &pos.x);
            pos.z = 0.0f;
            fm_vec3_scale(&pos, &pos, tower_ground_check_locs[j].r);
            fm_vec3_add(&pos, &pos, &towers[i].pos);
            pos.z = FLT_MAX;
            float loc_z;
            if (polycol_raycast_down(ground_polycol, &pos, &loc_z)) {
                z = MAX(z, loc_z);
            }
        }
        towers[i].pos.z = z;
    }

    rspq_block_begin();
    draw_primitive(ground_primitive);
    rspq_block_t *ground_block = rspq_block_end();

    struct {
        fm_vec3_t pos;
        struct generate_flower_texture_res flower_tex_res;
    } flowers[30];

    for (int i = 0; i < ARRAY_COUNT(flowers); i++) {
        float x, y;
        while (true) {
            x = rand() % (2 * GROUND_EXTENT + 1) - GROUND_EXTENT;
            y = rand() % (2 * GROUND_EXTENT + 1) - GROUND_EXTENT;
            bool too_close_to_a_tower = false;
            for (int j = 0; j < ARRAY_COUNT(towers); j++) {
                fm_vec2_t vec;
                fm_vec2_sub(&vec, &(fm_vec2_t){{x, y}},
                            &(fm_vec2_t){{towers[j].pos.x, towers[j].pos.y}});
                if (fm_vec2_len(&vec) < TOWER_RADIUS + 40.0f) {
                    too_close_to_a_tower = true;
                }
            }
            if (!too_close_to_a_tower) {
                break;
            }
        }
        float z = 0.0f;
        polycol_raycast_down(ground_polycol, &(fm_vec3_t){{x, y, FLT_MAX}}, &z);
        flowers[i].pos.x = x;
        flowers[i].pos.y = y;
        flowers[i].pos.z = z - 2.0f;
        success = generate_flower_texture(&flowers[i].flower_tex_res);
        assert(success);
    }

    rspq_block_begin();
    draw_primitive(flower_primitive);
    rspq_block_t *flower_block = rspq_block_end();

    while (true) {
        // get and initialize GfxCtx
        struct GfxCtx *gfx_ctx = &gfx_ctx_buf[i_gfx_ctx];
        i_gfx_ctx++;
        i_gfx_ctx %= display_get_num_buffers();
        gfx_ctx->i_ud_mat_buf = 0;

        for (int i = 0; i < ARRAY_COUNT(towers); i++) {
            if (!towers[i].rebuild_tower) {
                continue;
            }
            if (towers[i].tower != NULL) {
                free_tower(towers[i].tower);
                towers[i].tower = NULL;
            }
            if (towers[i].tower_primitive != NULL) {
                rspq_call_deferred(geom_mesh_free_primitive_voidp,
                                   towers[i].tower_primitive);
                towers[i].tower_primitive = NULL;
            }
            if (towers[i].tower_walls_primitive != NULL) {
                rspq_call_deferred(geom_mesh_free_primitive_voidp,
                                   towers[i].tower_walls_primitive);
                towers[i].tower_walls_primitive = NULL;
            }
            if (towers[i].tower_block != NULL) {
                rspq_call_deferred(rspq_block_free_voidp,
                                   towers[i].tower_block);
                towers[i].tower_block = NULL;
            }
            if (towers[i].tower_walls_block != NULL) {
                rspq_call_deferred(rspq_block_free_voidp,
                                   towers[i].tower_walls_block);
                towers[i].tower_walls_block = NULL;
            }
            if (towers[i].tex.tex.buffer != NULL) {
                rdpq_call_deferred(free, towers[i].tex.tex.buffer);
                towers[i].tex.tex.buffer = NULL;
            }
            if (towers[i].ariadnes_thread.thread_primitive != NULL) {
                rspq_call_deferred(geom_mesh_free_primitive_voidp,
                                   towers[i].ariadnes_thread.thread_primitive);
                towers[i].ariadnes_thread.thread_primitive = NULL;
            }
            if (towers[i].ariadnes_thread.thread_block != NULL) {
                rspq_call_deferred(rspq_block_free_voidp,
                                   towers[i].ariadnes_thread.thread_block);
                towers[i].ariadnes_thread.thread_block = NULL;
            }

            // malloc the tower data, with unset walls
            towers[i].tower =
                malloc_tower(5, 10, WF_VERTICAL_UNSET | WF_HORIZONTAL_UNSET);
            assert(towers[i].tower != NULL);

            towers[i].seed =
                (unsigned int)getentropy32() + (unsigned int)get_ticks();
            srand(towers[i].seed);

            // randomly add corridors
            int n_corridors = 1 + rand() % 2;
            if (rand() % 3 == 0) {
                n_corridors += 1;
            }
            for (int k = 0; k < n_corridors; k++) {
                towers[i]
                    .tower->floors[rand() % towers[i].tower->n_floors]
                    .corridor =
                    rand() % (towers[i].tower->segments_per_floor / 2);
            }

            // randomly build walls
            build_tower_walls(towers[i].tower);

            // Generate the primitives that will be drawn
            towers[i].tower_primitive =
                generate_tower_geometry(towers[i].tower, TOWER_RADIUS);
            assert(towers[i].tower_primitive != NULL);
            towers[i].tower_walls_primitive =
                generate_tower_walls_geometry(towers[i].tower, TOWER_RADIUS);
            assert(towers[i].tower_walls_primitive != NULL);

            success = generate_brick_texture(&towers[i].tex, 64, 32);
            assert(success);

            data_cache_writeback_invalidate_all();

            rspq_block_begin();
            draw_primitive(towers[i].tower_primitive);
            towers[i].tower_block = rspq_block_end();
            rspq_block_begin();
            draw_primitive(towers[i].tower_walls_primitive);
            towers[i].tower_walls_block = rspq_block_end();

            towers[i].ariadnes_thread.n_positions = 0;

            towers[i].rebuild_tower = false;
        }

        surface_t *surf = display_get();
        uint64_t t = get_ticks_ms();
        float dt = display_get_delta_time();
        joypad_poll();
        joypad_inputs_t inputs = joypad_get_inputs(JOYPAD_PORT_1);
        joypad_buttons_t pressed = joypad_get_buttons_pressed(JOYPAD_PORT_1);

        cam_switch_timer -= dt;
        if (cam_switch_timer < 0.0f) {
            cam_switch_timer = 0.0f;
        }

        fm_vec3_t eye, target, up;

        if (is_climbing_tower) {
            /*
             * Input and collision handling
             */

            // Horizontal movement
            tower_climb_ctx.suzanne_angle +=
                inputs.stick_x / 60.0f * FM_DEG2RAD(100) * dt;
            tower_climb_ctx.suzanne_angle =
                fm_wrap_angle(tower_climb_ctx.suzanne_angle);
            {
                int segment =
                    (int)fm_roundf(
                        tower_climb_ctx.suzanne_angle /
                        (2 * FM_PI /
                         towers[i_cur_tower].tower->segments_per_floor)) %
                    towers[i_cur_tower].tower->segments_per_floor;
                int floor = (int)fm_roundf(tower_climb_ctx.suzanne_height);
                if (floor >= 0 && floor < towers[i_cur_tower].tower->n_floors) {
                    if (towers[i_cur_tower]
                            .tower->floors[floor]
                            .wall_flags[segment] &
                        WF_VERTICAL) {
                        float limit =
                            (segment * 2 * FM_PI - FM_DEG2RAD(50)) /
                            towers[i_cur_tower].tower->segments_per_floor;
                        if (fm_wrap_angle(tower_climb_ctx.suzanne_angle -
                                          limit) >= FM_PI) {
                            tower_climb_ctx.suzanne_angle = limit;
                        }
                    }
                    if (towers[i_cur_tower].tower->floors[floor].wall_flags
                            [(segment + 1) %
                             towers[i_cur_tower].tower->segments_per_floor] &
                        WF_VERTICAL) {
                        float limit =
                            (segment * 2 * FM_PI + FM_DEG2RAD(50)) /
                            towers[i_cur_tower].tower->segments_per_floor;
                        if (fm_wrap_angle(tower_climb_ctx.suzanne_angle -
                                          limit) < FM_PI) {
                            tower_climb_ctx.suzanne_angle = limit;
                        }
                    }
                }
            }
            if (abs(inputs.stick_x) < 10) {
                float target_suzanne_angle =
                    fm_roundf(tower_climb_ctx.suzanne_angle /
                              (2 * FM_PI /
                               towers[i_cur_tower].tower->segments_per_floor)) *
                    (2 * FM_PI / towers[i_cur_tower].tower->segments_per_floor);
                tower_climb_ctx.suzanne_angle =
                    fm_lerp_angle(tower_climb_ctx.suzanne_angle,
                                  target_suzanne_angle, 0.1f * 60 * dt);
            }

            // Going through corridors
            if (pressed.a) {
                int floor = (int)fm_roundf(tower_climb_ctx.suzanne_height);
                if (floor >= 0 && floor < towers[i_cur_tower].tower->n_floors) {
                    int corridor =
                        towers[i_cur_tower].tower->floors[floor].corridor;
                    if (corridor != -1) {
                        int segment =
                            (int)fm_roundf(tower_climb_ctx.suzanne_angle /
                                           (2 * FM_PI /
                                            towers[i_cur_tower]
                                                .tower->segments_per_floor)) %
                            towers[i_cur_tower].tower->segments_per_floor;
                        if (corridor == segment ||
                            corridor + towers[i_cur_tower]
                                               .tower->segments_per_floor /
                                           2 ==
                                segment) {
                            tower_climb_ctx.suzanne_angle += FM_PI;
                        }
                    }
                }
            }

            if (pressed.z) {
                towers[i_cur_tower].rebuild_tower = true;
            }

            // Vertical movement
            tower_climb_ctx.suzanne_height += inputs.stick_y / 60.0f * dt;
            if (tower_climb_ctx.suzanne_height < -0.5f + SUZANNE_CHIN_Z_M) {
                tower_climb_ctx.suzanne_height = -0.5f + SUZANNE_CHIN_Z_M;
            }
            if (tower_climb_ctx.suzanne_height >
                towers[i_cur_tower].tower->n_floors - 1 + 0.5f -
                    SUZANNE_SCALP_Z_M) {
                tower_climb_ctx.suzanne_height =
                    towers[i_cur_tower].tower->n_floors - 1 + 0.5f -
                    SUZANNE_SCALP_Z_M;
            }
            {
                int segment =
                    (int)fm_roundf(
                        tower_climb_ctx.suzanne_angle /
                        (2 * FM_PI /
                         towers[i_cur_tower].tower->segments_per_floor)) %
                    towers[i_cur_tower].tower->segments_per_floor;
                int floor = (int)fm_roundf(tower_climb_ctx.suzanne_height);
                if (floor >= 0 && floor < towers[i_cur_tower].tower->n_floors) {
                    if (towers[i_cur_tower]
                            .tower->floors[floor]
                            .wall_flags[segment] &
                        WF_HORIZONTAL) {
                        float limit = floor + 0.5f - SUZANNE_SCALP_Z_M;
                        if (tower_climb_ctx.suzanne_height > limit) {
                            tower_climb_ctx.suzanne_height = limit;
                        }
                    }
                    if (floor > 0 && (towers[i_cur_tower]
                                          .tower->floors[floor - 1]
                                          .wall_flags[segment] &
                                      WF_HORIZONTAL)) {
                        float limit = floor - 0.5f + SUZANNE_CHIN_Z_M;
                        if (tower_climb_ctx.suzanne_height < limit) {
                            tower_climb_ctx.suzanne_height = limit;
                        }
                    }
                }
                if (towers[i_cur_tower].ariadnes_thread.n_positions == 0) {
                    towers[i_cur_tower].ariadnes_thread.n_positions = 1;
                } else if (towers[i_cur_tower].ariadnes_thread.n_positions >=
                               2 &&
                           towers[i_cur_tower]
                                   .ariadnes_thread
                                   .positions[towers[i_cur_tower]
                                                  .ariadnes_thread.n_positions -
                                              2]
                                   .floor == floor &&
                           towers[i_cur_tower]
                                   .ariadnes_thread
                                   .positions[towers[i_cur_tower]
                                                  .ariadnes_thread.n_positions -
                                              2]
                                   .segment == segment) {
                    towers[i_cur_tower].ariadnes_thread.n_positions -= 1;
                } else if (!(towers[i_cur_tower]
                                     .ariadnes_thread
                                     .positions[towers[i_cur_tower]
                                                    .ariadnes_thread
                                                    .n_positions -
                                                1]
                                     .floor == floor &&
                             towers[i_cur_tower]
                                     .ariadnes_thread
                                     .positions[towers[i_cur_tower]
                                                    .ariadnes_thread
                                                    .n_positions -
                                                1]
                                     .segment == segment)) {
                    towers[i_cur_tower].ariadnes_thread.n_positions += 1;
                }
                if (towers[i_cur_tower].ariadnes_thread.n_positions >
                    towers[i_cur_tower].ariadnes_thread.max_positions) {
                    int new_max_positions =
                        towers[i_cur_tower].ariadnes_thread.max_positions * 2;
                    void *tmp = realloc(
                        towers[i_cur_tower].ariadnes_thread.positions,
                        sizeof(
                            towers[i_cur_tower].ariadnes_thread.positions[0]) *
                            new_max_positions);
                    if (tmp == NULL) {
                        towers[i_cur_tower].ariadnes_thread.n_positions =
                            towers[i_cur_tower].ariadnes_thread.max_positions;
                    } else {
                        towers[i_cur_tower].ariadnes_thread.positions = tmp;
                        towers[i_cur_tower].ariadnes_thread.max_positions =
                            new_max_positions;
                    }
                }
                assert(towers[i_cur_tower].ariadnes_thread.n_positions <=
                       towers[i_cur_tower].ariadnes_thread.max_positions);
                towers[i_cur_tower]
                    .ariadnes_thread
                    .positions[towers[i_cur_tower].ariadnes_thread.n_positions -
                               1]
                    .floor = floor;
                towers[i_cur_tower]
                    .ariadnes_thread
                    .positions[towers[i_cur_tower].ariadnes_thread.n_positions -
                               1]
                    .segment = segment;
            }
            if (abs(inputs.stick_y) < 10) {
                float target_suzanne_height =
                    fm_roundf(tower_climb_ctx.suzanne_height);
                tower_climb_ctx.suzanne_height =
                    fm_lerp(tower_climb_ctx.suzanne_height,
                            target_suzanne_height, 0.1f * 60 * dt);
            }

            if (pressed.b) {
                is_climbing_tower = false;
                cam_switch_timer = cam_switch_timer_ini = 1.0f;
            }

            /*
             * Camera/view handling
             */

            tower_climb_ctx.camera_angle =
                fm_lerp_angle(tower_climb_ctx.camera_angle,
                              tower_climb_ctx.suzanne_angle, 0.1f * 60 * dt);
            tower_climb_ctx.camera_at_height =
                fm_lerp_angle(tower_climb_ctx.camera_at_height,
                              tower_climb_ctx.suzanne_height, 0.2f * 60 * dt);
            tower_climb_ctx.camera_eye_height =
                fm_lerp_angle(tower_climb_ctx.camera_eye_height,
                              tower_climb_ctx.camera_at_height, 0.1f * 60 * dt);

            fm_sincosf(tower_climb_ctx.camera_angle, &eye.y, &eye.x);
            eye.z = 0.0f;
            fm_vec3_scale(&eye, &eye, 2.0f * TOWER_RADIUS);
            eye.z = (tower_climb_ctx.camera_eye_height + 0.5f) * TOWER_RADIUS;
            target = (fm_vec3_t){{
                0,
                0,
                (tower_climb_ctx.camera_at_height + 0.5f) * TOWER_RADIUS,
            }};
            fm_vec3_add(&eye, &eye, &towers[i_cur_tower].pos);
            fm_vec3_add(&target, &target, &towers[i_cur_tower].pos);
            up = (fm_vec3_t){{0, 0, 1}};
        } else {
            fm_mat3_t mat;
            fm_mat3_identity(&mat);
            fm_mat3_rotate(&mat, -free_roam_ctx.camera_yaw);
            fm_vec3_t d;
            float f = 1 / 60.0f * 10.0f * 60 * dt;
            fm_mat3_mul_vec2(
                &d, &mat,
                &(fm_vec2_t){{inputs.stick_x * f, inputs.stick_y * f}});
            free_roam_ctx.suzanne_pos.x += d.x;
            free_roam_ctx.suzanne_pos.y += d.y;
            if (fm_vec2_len(&(fm_vec2_t){{inputs.stick_x, inputs.stick_y}}) >
                20) {
                // atan2(stick_y, stick_x) gives an angle from +x (rightward)
                // subtract pi/2 to get an angle from +y (forward)
                free_roam_ctx.suzanne_yaw = fm_wrap_angle(
                    free_roam_ctx.camera_yaw +
                    fm_atan2f(inputs.stick_y, inputs.stick_x) - FM_PI / 2);
                free_roam_ctx.suzanne_yaw_idle_time = 0.0f;
            } else {
                free_roam_ctx.suzanne_yaw_idle_time += dt;
            }

            int i_closest_tower = -1;
            float closest_tower_dist = 0.0f;
            for (int i = 0; i < ARRAY_COUNT(towers); i++) {
                fm_vec3_t diff;
                fm_vec3_sub(&diff, &towers[i].pos, &free_roam_ctx.suzanne_pos);
                float dist = fm_vec3_len(&diff);
                if (i_closest_tower == -1 || dist < closest_tower_dist) {
                    i_closest_tower = i;
                    closest_tower_dist = dist;
                }
            }
            if (closest_tower_dist < TOWER_RADIUS + SUZANNE_RADIUS) {
                fm_vec2_t diff;
                if (closest_tower_dist > FM_EPSILON) {
                    fm_vec2_sub(&diff,
                                &(fm_vec2_t){{
                                    free_roam_ctx.suzanne_pos.x,
                                    free_roam_ctx.suzanne_pos.y,
                                }},
                                &(fm_vec2_t){{
                                    towers[i_closest_tower].pos.x,
                                    towers[i_closest_tower].pos.y,
                                }});
                    fm_vec2_scale(&diff, &diff,
                                  (TOWER_RADIUS + SUZANNE_RADIUS) /
                                      closest_tower_dist);
                } else {
                    diff = (fm_vec2_t){{TOWER_RADIUS + SUZANNE_RADIUS, 0.0f}};
                }
                fm_vec2_t pos;
                fm_vec2_add(&pos,
                            &(fm_vec2_t){{
                                towers[i_closest_tower].pos.x,
                                towers[i_closest_tower].pos.y,
                            }},
                            &diff);
                free_roam_ctx.suzanne_pos.x = pos.x;
                free_roam_ctx.suzanne_pos.y = pos.y;
            }

            float intersectZ;
            fm_vec3_t raycast_from = free_roam_ctx.suzanne_pos;
            raycast_from.z += SUZANNE_CHIN_Z_M * 100.0f;
            bool hit = polycol_raycast_down(ground_polycol, &raycast_from,
                                            &intersectZ);
            if (hit) {
                free_roam_ctx.suzanne_pos.z = intersectZ;
            }

            if (pressed.a &&
                closest_tower_dist < TOWER_RADIUS + SUZANNE_RADIUS * 2) {
                is_climbing_tower = true;
                cam_switch_timer = cam_switch_timer_ini = 1.0f;
                i_cur_tower = i_closest_tower;
                towers[i_cur_tower].ariadnes_thread.n_positions = 0;
                if (towers[i_cur_tower].ariadnes_thread.thread_primitive !=
                    NULL) {
                    rspq_call_deferred(
                        geom_mesh_free_primitive_voidp,
                        towers[i_cur_tower].ariadnes_thread.thread_primitive);
                    towers[i_cur_tower].ariadnes_thread.thread_primitive = NULL;
                }
                if (towers[i_cur_tower].ariadnes_thread.thread_block != NULL) {
                    rspq_call_deferred(
                        rspq_block_free_voidp,
                        towers[i_cur_tower].ariadnes_thread.thread_block);
                    towers[i_cur_tower].ariadnes_thread.thread_block = NULL;
                }
                memset(&tower_climb_ctx, 0, sizeof(tower_climb_ctx));
                if (closest_tower_dist > FM_EPSILON) {
                    fm_vec2_t diff;
                    fm_vec2_sub(&diff,
                                &(fm_vec2_t){{
                                    free_roam_ctx.suzanne_pos.x,
                                    free_roam_ctx.suzanne_pos.y,
                                }},
                                &(fm_vec2_t){{
                                    towers[i_closest_tower].pos.x,
                                    towers[i_closest_tower].pos.y,
                                }});
                    tower_climb_ctx.suzanne_angle =
                        tower_climb_ctx.camera_angle =
                            fm_atan2f(diff.y, diff.x);
                }
            }

            /*
             * Camera/view handling
             */

            if (fabsf(fm_wrap_angle(free_roam_ctx.camera_yaw -
                                    free_roam_ctx.suzanne_yaw) -
                      FM_PI) > FM_DEG2RAD(60) ||
                free_roam_ctx.suzanne_yaw_idle_time > 1) {
                free_roam_ctx.camera_yaw = fm_wrap_angle(my_lerp_angle_maxed(
                    free_roam_ctx.camera_yaw, free_roam_ctx.suzanne_yaw,
                    0.1f * 60 * dt, FM_DEG2RAD(180.0f / 60) * 60 * dt));
            }

            target = free_roam_ctx.suzanne_pos;
            target.z += SUZANNE_CHIN_Z_M * 100;
            fm_vec3_t eyeToTarget;
            // camera_yaw = 0 -> (0,1) (forward)
            // camera_yaw = pi/2 -> (-1,0) (leftward)
            fm_sincosf(free_roam_ctx.camera_yaw + FM_PI / 2, &eyeToTarget.y,
                       &eyeToTarget.x);
            eyeToTarget.z = 0.0f;
            fm_vec3_scale(&eyeToTarget, &eyeToTarget, 200.0f);
            fm_vec3_sub(&eye, &target, &eyeToTarget);
            fm_vec3_t eye_raycast_from = eye;
            eye_raycast_from.z += 100.0f;
            float eye_ground_z;
            if (polycol_raycast_down(ground_polycol, &eye_raycast_from,
                                     &eye_ground_z)) {
                eye.z = MAX(eye.z, eye_ground_z + 10.0f);
            }
            up = (fm_vec3_t){{0, 0, 1}};
        }

        if (cam_switch_timer == 0.0f) {
            cam_eye = eye;
            cam_target = target;
            cam_up = up;
        } else {
            fm_vec3_lerp(&cam_eye, &cam_eye, &eye,
                         1.0f - cam_switch_timer / cam_switch_timer_ini);
            fm_vec3_lerp(&cam_target, &cam_target, &target,
                         1.0f - cam_switch_timer / cam_switch_timer_ini);
            fm_vec3_lerp(&cam_up, &cam_up, &up,
                         1.0f - cam_switch_timer / cam_switch_timer_ini);
        }

        fm_mat4_t mat_view;
        fm_mat4_lookat(&mat_view, &cam_eye, &cam_target, &cam_up);

        /*
         * Drawing
         */

        rdpq_attach(surf, display_get_zbuf());

        rdpq_clear((color_t){
            94,
            169,
            227,
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

        mg_pipeline_bind(pipeline);

        mg_set_viewport(&(mg_viewport_t){
            .x = 0,
            .y = 0,
            .width = display_get_width(),
            .height = display_get_height(),
            .minDepth = 0.0f,
            .maxDepth = 1.0f,
            .z_near = Z_NEAR,
            .z_far = Z_FAR,
        });

        mg_set_culling(&(mg_culling_parms_t){.cull_mode = MG_CULL_MODE_BACK});

        mg_set_geometry_flags(MG_GEOMETRY_FLAGS_SHADE_ENABLED |
                              MG_GEOMETRY_FLAGS_Z_ENABLED);

        mg_uniform_load(u_fog, &ud_fog);
        mg_uniform_load(u_lighting, &ud_lighting);

        // Draw Suzanne
        rdpq_set_prim_color((color_t){255, 100, 100, 255});
        {
            fm_mat4_t mat_model;
            fm_mat4_identity(&mat_model);
            if (is_climbing_tower) {
                fm_vec3_t translate = {
                    {0.0f, -110.0f,
                     (tower_climb_ctx.suzanne_height + 0.5f) * 100.0f}};
                fm_mat4_translate(&mat_model, &translate);
                fm_quat_t rotation;
                fm_quat_from_euler_zyx(&rotation, 0.0f, 0.0f,
                                       tower_climb_ctx.suzanne_angle +
                                           FM_PI / 2);
                fm_mat4_rotate(&mat_model, &rotation);
                fm_mat4_translate(&mat_model, &towers[i_cur_tower].pos);
            } else {
                fm_quat_t rotation;
                // Suzanne model looks towards -y (backwards)
                fm_quat_from_euler_zyx(&rotation, 0.0f, 0.0f,
                                       free_roam_ctx.suzanne_yaw + FM_PI);
                fm_mat4_rotate(&mat_model, &rotation);
                fm_vec3_t translate = free_roam_ctx.suzanne_pos;
                translate.z += SUZANNE_CHIN_Z_M * 100;
                fm_mat4_translate(&mat_model, &translate);
            }
            mgfx_matrices_t *ud_matrices =
                build_matrices(gfx_ctx, &mat_projection, &mat_view, &mat_model);

            mg_uniform_load(u_matrices, ud_matrices);

            rspq_block_run(suzanne_block);
        }

        mg_pipeline_bind(textured_pipeline);

        mg_set_culling(&(mg_culling_parms_t){.cull_mode = MG_CULL_MODE_BACK});

        mg_set_geometry_flags(MG_GEOMETRY_FLAGS_TEX_ENABLED |
                              MG_GEOMETRY_FLAGS_SHADE_ENABLED |
                              MG_GEOMETRY_FLAGS_Z_ENABLED);

        mg_uniform_load(textured_u_fog, &textured_ud_fog);
        mg_uniform_load(textured_u_texturing, &textured_ud_texturing);
        mg_uniform_load(textured_u_lighting, &ud_lighting);

        rdpq_mode_combiner(RDPQ_COMBINER_TEX_SHADE);
        rdpq_mode_persp(true);
        rdpq_mode_filter(FILTER_BILINEAR);
        for (int i = 0; i < ARRAY_COUNT(towers); i++) {
            if (towers[i].tower == NULL) {
                continue;
            }
            // Draw the tower
            mg_set_culling(
                &(mg_culling_parms_t){.cull_mode = MG_CULL_MODE_BACK});
            rdpq_set_prim_color((color_t){100, 100, 255, 255});
            {
                fm_mat4_t mat_model;
                fm_mat4_identity(&mat_model);
                fm_mat4_translate(&mat_model, &towers[i].pos);
                mgfx_matrices_t *ud_matrices = build_matrices(
                    gfx_ctx, &mat_projection, &mat_view, &mat_model);

                mg_uniform_load(u_matrices, ud_matrices);

                rdpq_tex_upload(TILE0, &towers[i].tex.tex,
                                &(rdpq_texparms_t){
                                    .s.repeats = REPEAT_INFINITE,
                                    .s.scale_log = 0,
                                    .t.repeats = REPEAT_INFINITE,
                                    .t.scale_log = -1,
                                });
                rspq_block_run(towers[i].tower_block);
            }
        }

        mg_set_geometry_flags(MG_GEOMETRY_FLAGS_TEX_ENABLED |
                              MG_GEOMETRY_FLAGS_Z_ENABLED);

        rdpq_mode_combiner(RDPQ_COMBINER1((TEX0, 0, TEX1, 0), (0, 0, 0, 1)));
        rdpq_mode_persp(true);
        rdpq_mode_filter(FILTER_BILINEAR);
        rdpq_tex_multi_begin();
        rdpq_tex_upload(TILE0, &ground_tex_res.multitex_color,
                        &(rdpq_texparms_t){
                            .s.repeats = REPEAT_INFINITE,
                            .t.repeats = REPEAT_INFINITE,
                            .s.scale_log = GROUND_ST_SHIFT,
                            .t.scale_log = GROUND_ST_SHIFT,
                        });
        rdpq_tex_upload(TILE1, &ground_tex_res.multitex_gray,
                        &(rdpq_texparms_t){
                            .s.repeats = REPEAT_INFINITE,
                            .t.repeats = REPEAT_INFINITE,
                            .s.scale_log = GROUND_ST_SHIFT - 4,
                            .t.scale_log = GROUND_ST_SHIFT - 4,
                        });
        rdpq_tex_multi_end();
        {
            fm_mat4_t mat_model;
            fm_mat4_identity(&mat_model);
            float s = 1.0f;
            fm_mat4_scale(&mat_model, &(fm_vec3_t){{s, s, s}});
            mgfx_matrices_t *textured_ud_matrices =
                build_matrices(gfx_ctx, &mat_projection, &mat_view, &mat_model);

            mg_uniform_load(textured_u_matrices, textured_ud_matrices);

            rspq_block_run(ground_block);
        }

        mg_set_culling(&(mg_culling_parms_t){.cull_mode = MG_CULL_MODE_BACK});
        rdpq_mode_begin();
        rdpq_set_mode_standard();
        rdpq_mode_antialias(AA_STANDARD);
        rdpq_mode_dithering(DITHER_SQUARE_SQUARE);
        rdpq_mode_zbuf(true, true);
        rdpq_mode_persp(true);
        rdpq_mode_filter(FILTER_BILINEAR);
        rdpq_mode_combiner(RDPQ_COMBINER_TEX);
        rdpq_mode_alphacompare(100);
        rdpq_mode_tlut(TLUT_RGBA16);
        rdpq_mode_end();
        for (int i = 0; i < ARRAY_COUNT(flowers); i++) {
            fm_mat4_t mat_model;
            fm_mat4_identity(&mat_model);
            fm_quat_t rotation;
            fm_quat_from_euler_zyx(
                &rotation, 0, 0,
                is_climbing_tower ? (tower_climb_ctx.camera_angle - FM_PI / 2)
                                  : (free_roam_ctx.camera_yaw + FM_PI));
            fm_mat4_rotate(&mat_model, &rotation);
            fm_mat4_translate(&mat_model, &flowers[i].pos);
            mgfx_matrices_t *textured_ud_matrices =
                build_matrices(gfx_ctx, &mat_projection, &mat_view, &mat_model);

            mg_uniform_load(textured_u_matrices, textured_ud_matrices);

            rdpq_tex_upload_tlut(flowers[i].flower_tex_res.tlut, 0,
                                 flowers[i].flower_tex_res.tlut_count);
            rdpq_tex_upload(TILE0, &flowers[i].flower_tex_res.tex,
                            &(rdpq_texparms_t){});
            rspq_block_run(flower_block);
        }

        mg_pipeline_bind(pipeline);

        mg_set_geometry_flags(MG_GEOMETRY_FLAGS_SHADE_ENABLED |
                              MG_GEOMETRY_FLAGS_Z_ENABLED);

        mg_uniform_load(u_fog, &ud_fog);
        mg_uniform_load(u_lighting, &ud_lighting);

        mg_set_culling(&(mg_culling_parms_t){.cull_mode = MG_CULL_MODE_NONE});

        rdpq_mode_begin();
        rdpq_set_mode_standard();
        rdpq_mode_antialias(AA_STANDARD);
        rdpq_mode_dithering(DITHER_SQUARE_SQUARE);
        rdpq_mode_zbuf(true, true);
        rdpq_mode_combiner(RDPQ_COMBINER_FLAT);
        rdpq_mode_end();
        rdpq_set_prim_color((color_t){168, 129, 71, 255});
        mg_draw_begin();
        for (int i_tower = 0; i_tower < ARRAY_COUNT(towers); i_tower++) {
            bool is_tower_being_climbed =
                is_climbing_tower && i_tower == i_cur_tower;
            bool use_block = !is_tower_being_climbed;
            bool record_block =
                towers[i_tower].ariadnes_thread.thread_block == NULL &&
                use_block;
            bool generate_geometry = record_block || !use_block;
            if (generate_geometry) {
                int n_positions = towers[i_tower].ariadnes_thread.n_positions;
                if (is_tower_being_climbed) {
                    n_positions += 1;
                }
                fm_vec3_t *positions = malloc(sizeof(fm_vec3_t) * n_positions);
                if (is_tower_being_climbed) {
                    fm_vec3_t pos;
                    fm_sincosf(tower_climb_ctx.suzanne_angle, &pos.y, &pos.x);
                    pos.x *= (TOWER_RADIUS + 10.0f);
                    pos.y *= (TOWER_RADIUS + 10.0f);
                    pos.z =
                        (tower_climb_ctx.suzanne_height + 0.5f) * TOWER_RADIUS;
                    fm_vec3_add(&pos, &pos, &towers[i_tower].pos);
                    positions[n_positions - 1] = pos;
                }
                for (int i = 0; i < towers[i_tower].ariadnes_thread.n_positions;
                     i++) {
                    fm_vec3_t pos;

                    fm_sincosf(
                        towers[i_tower].ariadnes_thread.positions[i].segment *
                            2 * FM_PI /
                            towers[i_tower].tower->segments_per_floor,
                        &pos.y, &pos.x);
                    pos.x *= (TOWER_RADIUS + 10.0f);
                    pos.y *= (TOWER_RADIUS + 10.0f);
                    pos.z =
                        (towers[i_tower].ariadnes_thread.positions[i].floor +
                         0.5f) *
                        TOWER_RADIUS;

                    fm_vec3_add(&pos, &pos, &towers[i_tower].pos);
                    positions[i] = pos;
                }
                if (is_tower_being_climbed && n_positions >= 3) {
                    fm_vec3_t *prev_pos = &positions[n_positions - 3];
                    fm_vec3_t *pos = &positions[n_positions - 2];
                    fm_vec3_t *suzanne_pos = &positions[n_positions - 1];
                    fm_vec3_t prev_to_pos, prev_to_suzanne;
                    fm_vec3_sub(&prev_to_pos, pos, prev_pos);
                    fm_vec3_sub(&prev_to_suzanne, suzanne_pos, prev_pos);
                    if (fm_vec3_len2(&prev_to_suzanne) <
                        fm_vec3_len2(&prev_to_pos)) {
                        *pos = *suzanne_pos;
                        n_positions -= 1;
                    }
                }
                assert(towers[i_tower].ariadnes_thread.thread_primitive ==
                       NULL);
                towers[i_tower].ariadnes_thread.thread_primitive =
                    generate_thread_geometry(positions, n_positions, 4.0f);
                free(positions);
                data_cache_writeback_invalidate_all();
            }

            fm_mat4_t mat_model;
            fm_mat4_identity(&mat_model);
            mgfx_matrices_t *ud_matrices =
                build_matrices(gfx_ctx, &mat_projection, &mat_view, &mat_model);
            mg_uniform_load(u_matrices, ud_matrices);
            if (record_block) {
                rspq_block_begin();
            }
            draw_primitive(towers[i_tower].ariadnes_thread.thread_primitive);
            if (record_block) {
                assert(towers[i_tower].ariadnes_thread.thread_block == NULL);
                towers[i_tower].ariadnes_thread.thread_block = rspq_block_end();
            }
            if (use_block) {
                assert(towers[i_tower].ariadnes_thread.thread_block != NULL);
                rspq_block_run(towers[i_tower].ariadnes_thread.thread_block);
            } else {
                rspq_call_deferred(
                    geom_mesh_free_primitive_voidp,
                    towers[i_tower].ariadnes_thread.thread_primitive);
                towers[i_tower].ariadnes_thread.thread_primitive = NULL;
            }
        }

        rdpq_set_prim_color((color_t){
            200,
            50,
            200,
            140 + 5 * cosf(t * 2 * FM_PI / 500) +
                20 * cosf(t * 2 * FM_PI / 2000) +
                40 * cosf(t * 2 * FM_PI / 10000),
        });
        rdpq_mode_begin();
        rdpq_set_mode_standard();
        rdpq_mode_antialias(AA_STANDARD);
        rdpq_mode_dithering(DITHER_SQUARE_SQUARE);
        rdpq_mode_zbuf(true, false);
        rdpq_mode_combiner(
            RDPQ_COMBINER1((PRIM, 0, SHADE, 0), (0, 0, 0, PRIM)));
        rdpq_mode_blender(RDPQ_BLENDER_MULTIPLY);
        rdpq_mode_end();
        for (int i = 0; i < ARRAY_COUNT(towers); i++) {
            if (towers[i].tower == NULL) {
                continue;
            }
            // Draw the tower walls
            {
                fm_mat4_t mat_model;
                fm_mat4_identity(&mat_model);
                fm_mat4_translate(&mat_model, &towers[i].pos);
                mgfx_matrices_t *ud_matrices = build_matrices(
                    gfx_ctx, &mat_projection, &mat_view, &mat_model);

                mg_uniform_load(u_matrices, ud_matrices);

                rspq_block_run(towers[i].tower_walls_block);
            }
        }

        if (is_climbing_tower) {
            // Print the seed
            rdpq_set_mode_standard();
            rdpq_text_printf(&(rdpq_textparms_t){}, font_id, 10, 10, "0x%08X",
                             towers[i_cur_tower].seed);
        }

        rdpq_detach_show();
    }
}
