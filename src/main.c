// SPDX-FileCopyrightText: 2026 Dragorn421
// SPDX-License-Identifier: CC0-1.0

#include <stdint.h>

#include <libdragon.h>

#include "model.h"

#include "../assets/Suzanne.c"

int main(void) {
    debug_init_emulog();

#ifdef USE_JOYPAD
    joypad_init();
#endif

    display_init(RESOLUTION_320x240, DEPTH_16_BPP, 3, GAMMA_NONE,
                 FILTERS_RESAMPLE_ANTIALIAS_DEDITHER);
    rdpq_init();
    mg_init();

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
                                                {{0.7, 0.7, 0, 0}},
                                                {100, 100, 100, 0},
                                            },
                                        },
                                        1,
                                    });

    fm_mat4_t mat_projection;
    mg_mat4_perspective(&mat_projection, FM_DEG2RAD(100),
                        (float)display_get_width() / display_get_height(),
                        0.001f, 10.0f);

    while (true) {
        surface_t *surf = display_get();
        uint64_t t = get_ticks_ms();
#ifdef USE_JOYPAD
        joypad_poll();
        joypad_buttons_t buttons_cur = joypad_get_buttons(JOYPAD_PORT_1);
#endif

        fm_vec3_t eye;
        fm_sincosf(t * 2 * FM_PI / 5000, &eye.x, &eye.y);
        eye.z = 0;
        fm_vec3_scale(&eye, &eye, 2.0f);
        fm_vec3_t target = {{0, 0, 0}};
        fm_mat4_t mat_view;
        fm_mat4_lookat(&mat_view, &eye, &target, &(fm_vec3_t){{0, 0, 1}});

        fm_mat4_t mat_model;
        fm_mat4_identity(&mat_model);
        fm_mat4_t mat_view_model;
        fm_mat4_mul(&mat_view_model, &mat_view, &mat_model);
        fm_mat4_t mat_projection_view_model;
        fm_mat4_mul(&mat_projection_view_model, &mat_projection,
                    &mat_view_model);
        fm_mat4_t mat_view_model_inv, mat_normal;
        fm_mat4_inverse(&mat_view_model_inv, &mat_view_model);
        fm_mat4_transpose(&mat_normal, &mat_view_model_inv);
        mgfx_matrices_t ud_matrices;
        mgfx_get_matrices(&ud_matrices, &(mgfx_matrices_parms_t){
                                            mat_projection_view_model.m[0],
                                            mat_view_model.m[0],
                                            mat_normal.m[0],
                                        });

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
#ifdef USE_JOYPAD
        if (buttons_cur.a) {
            rdpq_mode_zbuf(false, true);
        }
#endif
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

        mg_set_geometry_flags(MG_GEOMETRY_FLAGS_SHADE_ENABLED
                              // | MG_GEOMETRY_FLAGS_Z_ENABLED
        );

        mg_uniform_load(u_fog, &ud_fog);
        mg_uniform_load(u_lighting, &ud_lighting);
        mg_uniform_load(u_matrices, &ud_matrices);

        mg_bind_vertex_buffer(Suzanne_0_vertices);
        mg_draw_indexed(
            &(mg_input_assembly_parms_t){
                MG_PRIMITIVE_TOPOLOGY_TRIANGLE_LIST,
                false,
            },
            Suzanne_0_indices,
            sizeof(Suzanne_0_indices) / sizeof(Suzanne_0_indices[0]), 0);

        rdpq_detach_show();
    }
}
