// SPDX-FileCopyrightText: 2026 Dragorn421
// SPDX-License-Identifier: CC0-1.0

#include <assert.h>
#include <math.h>
#include <stdbool.h>
#include <stdint.h>
#include <stdlib.h>

#include <libdragon.h>

#include "model.h"

#include "../assets/CylinderSegs.c"
#include "../assets/Suzanne.c"

#ifndef ARRAY_COUNT
#define ARRAY_COUNT(arr) (sizeof(arr) / sizeof(arr[0]))
#endif

struct GfxCtx {
    mgfx_matrices_t ud_mat_buf[10];
    int i_ud_mat_buf;
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

    fm_mat4_t mat_projection;
    mg_mat4_perspective(&mat_projection, FM_DEG2RAD(60),
                        (float)display_get_width() / display_get_height(), 0.1f,
                        10.0f);

    float suzanne_angle = 0.0f;
    float suzanne_height = 0.0f;
    float camera_angle = 0.0f;
    float camera_eye_height = 0.0f;
    float camera_at_height = 0.0f;

#define SEGMENTS_PER_FLOOR 10
    struct {
        int n_floors;
        int segments_per_floor;
    } tower = {5, SEGMENTS_PER_FLOOR};
    struct {
        int corridor;
        bool vertical_walls[SEGMENTS_PER_FLOOR];
        bool horizontal_walls[SEGMENTS_PER_FLOOR];
    } tower_floors[tower.n_floors];
    for (int i = 0; i < tower.n_floors; i++) {
        tower_floors[i].corridor = -1;
        for (int j = 0; j < tower.segments_per_floor; j++) {
            tower_floors[i].vertical_walls[j] = false;
            tower_floors[i].horizontal_walls[j] = false;
        }
    }
    tower_floors[0].vertical_walls[0] = true;
    tower_floors[0].horizontal_walls[0] = true;
    tower_floors[0].horizontal_walls[1] = true;
    tower_floors[1].vertical_walls[2] = true;
    tower_floors[1].corridor = 0;
    tower_floors[2].corridor = 1;

    while (true) {
        struct GfxCtx *gfx_ctx = &gfx_ctx_buf[i_gfx_ctx];
        i_gfx_ctx++;
        i_gfx_ctx %= display_get_num_buffers();
        gfx_ctx->i_ud_mat_buf = 0;

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
                               (2 * FM_PI / tower.segments_per_floor)) %
                tower.segments_per_floor;
            int floor = (int)fm_roundf(suzanne_height);
            if (floor >= 0 && floor < tower.n_floors) {
                if (tower_floors[floor].vertical_walls[segment]) {
                    float limit = (segment * 2 * FM_PI - FM_DEG2RAD(50)) /
                                  tower.segments_per_floor;
                    if (fm_wrap_angle(suzanne_angle - limit) >= FM_PI) {
                        suzanne_angle = limit;
                    }
                }
                if (tower_floors[floor]
                        .vertical_walls[(segment + 1) %
                                        tower.segments_per_floor]) {
                    float limit = (segment * 2 * FM_PI + FM_DEG2RAD(50)) /
                                  tower.segments_per_floor;
                    if (fm_wrap_angle(suzanne_angle - limit) < FM_PI) {
                        suzanne_angle = limit;
                    }
                }
            }
        }
        if (abs(inputs.stick_x) < 10) {
            float target_suzanne_angle =
                fm_roundf(suzanne_angle /
                          (2 * FM_PI / tower.segments_per_floor)) *
                (2 * FM_PI / tower.segments_per_floor);
            suzanne_angle = my_lerp_angle(suzanne_angle, target_suzanne_angle,
                                          0.1f * 60 * dt);
        }

        if (pressed.a) {
            int floor = (int)fm_roundf(suzanne_height);
            if (floor >= 0 && floor < tower.n_floors) {
                int corridor = tower_floors[floor].corridor;
                if (corridor != -1) {
                    int segment =
                        (int)fm_roundf(suzanne_angle /
                                       (2 * FM_PI / tower.segments_per_floor)) %
                        tower.segments_per_floor;
                    if (corridor == segment ||
                        corridor + tower.segments_per_floor / 2 == segment) {
                        suzanne_angle += FM_PI;
                    }
                }
            }
        }

        suzanne_height += inputs.stick_y / 60.0f * dt;
        if (suzanne_height < -0.3f) {
            suzanne_height = -0.3f;
        }
        if (suzanne_height > tower.n_floors - 1 + 0.3f) {
            suzanne_height = tower.n_floors - 1 + 0.3f;
        }
        {
            int segment =
                (int)fm_roundf(suzanne_angle /
                               (2 * FM_PI / tower.segments_per_floor)) %
                tower.segments_per_floor;
            int floor = (int)fm_roundf(suzanne_height);
            if (floor >= 0 && floor < tower.n_floors) {
                if (tower_floors[floor].horizontal_walls[segment]) {
                    float limit = floor + 0.3f;
                    if (suzanne_height > limit) {
                        suzanne_height = limit;
                    }
                }
                if (floor > 0 &&
                    tower_floors[floor - 1].horizontal_walls[segment]) {
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

            mg_bind_vertex_buffer(Suzanne_0_vertices);
            mg_draw_indexed(
                &(mg_input_assembly_parms_t){
                    MG_PRIMITIVE_TOPOLOGY_TRIANGLE_LIST,
                    false,
                },
                Suzanne_0_indices, ARRAY_COUNT(Suzanne_0_indices), 0);
        }

        rdpq_set_prim_color((color_t){100, 100, 255, 255});
        for (int i = 0; i < tower.n_floors; i++) {
            int corridor = tower_floors[i].corridor;

            fm_mat4_t mat_model;
            fm_mat4_identity(&mat_model);
            fm_mat4_translate(&mat_model,
                              &(fm_vec3_t){{0.0f, 0.0f, -0.5f + i}});
            if (corridor != -1) {
                fm_quat_t rotation;
                fm_quat_from_euler_zyx(&rotation, 0.0f, 0.0f,
                                       corridor * 2 * FM_PI /
                                           tower.segments_per_floor);
                fm_mat4_rotate(&mat_model, &rotation);
            }
            float s = 1.0f / 512;
            fm_mat4_scale(&mat_model, &(fm_vec3_t){{s, s, s}});
            mgfx_matrices_t *ud_matrices =
                build_matrices(gfx_ctx, &mat_projection, &mat_view, &mat_model);

            mg_uniform_load(u_matrices, ud_matrices);

            if (corridor == -1) {
                mg_bind_vertex_buffer(CylinderSeg_0_vertices);
                mg_draw_indexed(
                    &(mg_input_assembly_parms_t){
                        MG_PRIMITIVE_TOPOLOGY_TRIANGLE_LIST,
                        false,
                    },
                    CylinderSeg_0_indices, ARRAY_COUNT(CylinderSeg_0_indices),
                    0);
            } else {
                mg_bind_vertex_buffer(CylinderSegWithTunnel_0_vertices);
                mg_draw_indexed(
                    &(mg_input_assembly_parms_t){
                        MG_PRIMITIVE_TOPOLOGY_TRIANGLE_LIST,
                        false,
                    },
                    CylinderSegWithTunnel_0_indices,
                    ARRAY_COUNT(CylinderSegWithTunnel_0_indices), 0);
            }
        }

        mg_set_culling(&(mg_culling_parms_t){.cull_mode = MG_CULL_MODE_NONE});
        rdpq_set_prim_color((color_t){50, 50, 200, 255});
        for (int i = 0; i < tower.n_floors; i++) {
            for (int j = 0; j < tower.segments_per_floor; j++) {
                if (tower_floors[i].vertical_walls[j]) {
                    fm_mat4_t mat_model;
                    fm_mat4_identity(&mat_model);
                    fm_mat4_translate(&mat_model,
                                      &(fm_vec3_t){{0.0f, 0.0f, -0.5f + i}});
                    fm_quat_t rotation;
                    fm_quat_from_euler_zyx(&rotation, 0.0f, 0.0f,
                                           j * 2 * FM_PI /
                                               tower.segments_per_floor);
                    fm_mat4_rotate(&mat_model, &rotation);
                    float s = 1.0f / 512;
                    fm_mat4_scale(&mat_model, &(fm_vec3_t){{s, s, s}});
                    mgfx_matrices_t *ud_matrices = build_matrices(
                        gfx_ctx, &mat_projection, &mat_view, &mat_model);

                    mg_uniform_load(u_matrices, ud_matrices);

                    mg_bind_vertex_buffer(VerticalWall_0_vertices);
                    mg_draw_indexed(
                        &(mg_input_assembly_parms_t){
                            MG_PRIMITIVE_TOPOLOGY_TRIANGLE_LIST,
                            false,
                        },
                        VerticalWall_0_indices,
                        ARRAY_COUNT(VerticalWall_0_indices), 0);
                }
                if (tower_floors[i].horizontal_walls[j]) {
                    fm_mat4_t mat_model;
                    fm_mat4_identity(&mat_model);
                    fm_mat4_translate(&mat_model,
                                      &(fm_vec3_t){{0.0f, 0.0f, -0.5f + i}});
                    fm_quat_t rotation;
                    fm_quat_from_euler_zyx(&rotation, 0.0f, 0.0f,
                                           j * 2 * FM_PI /
                                               tower.segments_per_floor);
                    fm_mat4_rotate(&mat_model, &rotation);
                    float s = 1.0f / 512;
                    fm_mat4_scale(&mat_model, &(fm_vec3_t){{s, s, s}});
                    mgfx_matrices_t *ud_matrices = build_matrices(
                        gfx_ctx, &mat_projection, &mat_view, &mat_model);

                    mg_uniform_load(u_matrices, ud_matrices);

                    mg_bind_vertex_buffer(HorizontalWall_0_vertices);
                    mg_draw_indexed(
                        &(mg_input_assembly_parms_t){
                            MG_PRIMITIVE_TOPOLOGY_TRIANGLE_LIST,
                            false,
                        },
                        HorizontalWall_0_indices,
                        ARRAY_COUNT(HorizontalWall_0_indices), 0);
                }
            }
        }

        rdpq_detach_show();
    }
}
