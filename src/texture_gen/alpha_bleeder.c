// SPDX-FileCopyrightText: 2026 Dragorn421
// SPDX-License-Identifier: CC0-1.0

#include <assert.h>
#include <limits.h>
#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>
#include <stdlib.h>
#include <string.h>

struct Vec2i {
    int x, y;
};

/**
 * Replace the color of 0-alpha pixels with the color of the nearest visible
 * pixel.
 *
 * @param im A CI8 image of size width*height
 * @param tlut TLUT for im
 * @param tlut_count Amount of colors in tlut
 * @param out_im A buffer of size width*height to write the resulting image to
 * @param out_tlut A buffer of length 256 to contain the resulting TLUT
 * @param out_tlut_count The actual amount of colors in out_tlut
 * @return true on success
 */
bool alpha_bleeder(uint8_t *im, uint16_t *tlut, int tlut_count, int width,
                   int height, uint8_t *out_im, uint16_t *out_tlut,
                   int *out_tlut_count) {
#define IMACCESS(im, x, y) (im)[(y) * width + (x)]

    /*
     * This algorithm consists in starting by copying `im` to `out_im`,
     * then iteratively paint 0-alpha pixels of `out_im` with the color
     * of the closest visible pixel (including the non-0 alpha) until
     * there are no 0-alpha pixels.
     * Then at the end the alpha channel is restored by copying
     * the alpha from `im` to `out_im`.
     */

    memcpy(out_im, im, width * height);
    memcpy(out_tlut, tlut, tlut_count * 2);

    // The `closest` array tracks the pixels from which each 0-alpha
    // pixel got its color from.
    struct {
        struct Vec2i *coords;
        int n_coords;
    } *closest;
    struct Vec2i *expand_to;

    // allocate `closest` and `expand_to` in one call
    // so that they can be freed later with one call
    // as well. micro optimization
    closest = malloc(sizeof(*closest) * (height * width) +
                     sizeof(*expand_to) * (height * width));
    expand_to = (struct Vec2i *)(closest + (height * width));

    if (closest == NULL)
        return false;

    for (int y = 0; y < height; y++)
        for (int x = 0; x < width; x++)
            closest[y * width + x].coords = NULL;

    while (true) {
        // Find all 0-alpha pixels with visible neighbours in `out_im`
        // (including 0-alpha pixels from `im` that were processed)
        // and store their coordinates into `expand_to`

        int expand_to_len = 0;

        for (int y = 0; y < height; y++) {
            for (int x = 0; x < width; x++) {
                if ((out_tlut[IMACCESS(out_im, x, y)] & 1) == 0) {
                    struct Vec2i neighbours[] = {
                        {x, y - 1},
                        {x + 1, y},
                        {x, y + 1},
                        {x - 1, y},
                    };
                    for (int i = 0;
                         i < (int)(sizeof(neighbours) / sizeof(neighbours[0]));
                         i++) {
                        struct Vec2i *n = &neighbours[i];
                        if (n->x >= 0 && n->x < width && n->y >= 0 &&
                            n->y < height) {
                            if ((out_tlut[IMACCESS(out_im, n->x, n->y)] & 1) !=
                                0) {
                                expand_to[expand_to_len].x = x;
                                expand_to[expand_to_len].y = y;
                                expand_to_len++;
                                break;
                            }
                        }
                    }
                }
            }
        }

        // If there are no 0-alpha pixels left, we are done
        if (expand_to_len == 0)
            break;

        // For each 0-alpha pixel with visible neighbours,

        for (int j = 0; j < expand_to_len; j++) {
            int x = expand_to[j].x;
            int y = expand_to[j].y;
            int closest_pixels_buf_len = 1;
            struct Vec2i *closest_pixels =
                malloc(sizeof(closest_pixels[0]) * closest_pixels_buf_len);
            int n_closest_pixels = 0;
            int closest_pixels_dist_sq = INT_MAX;

            struct Vec2i neighbours[] = {
                {x, y - 1},
                {x + 1, y},
                {x, y + 1},
                {x - 1, y},
            };
            for (int i = 0;
                 i < (int)(sizeof(neighbours) / sizeof(neighbours[0])); i++) {
                struct Vec2i *n = &neighbours[i];
                if (n->x >= 0 && n->x < width && n->y >= 0 && n->y < height) {
                    if ((out_tlut[IMACCESS(out_im, n->x, n->y)] & 1) != 0) {
                        struct Vec2i *coords;
                        int n_coords;
                        if (closest[n->y * width + n->x].coords == NULL) {
                            coords = n;
                            n_coords = 1;
                        } else {
                            coords = closest[n->y * width + n->x].coords;
                            n_coords = closest[n->y * width + n->x].n_coords;
                        }
                        for (int k = 0; k < n_coords; k++) {
                            struct Vec2i *nc = &coords[k];
                            int nc_dist_sq = (nc->x - x) * (nc->x - x) +
                                             (nc->y - y) * (nc->y - y);
                            if (nc_dist_sq < closest_pixels_dist_sq) {
                                // closest_pixels = {nc}
                                closest_pixels[0] = *nc;
                                n_closest_pixels = 1;
                                closest_pixels_dist_sq = nc_dist_sq;
                            } else if (nc_dist_sq == closest_pixels_dist_sq) {
                                bool nc_already_in_closest_pixels = false;
                                for (int k = 0; k < n_closest_pixels; k++) {
                                    if (closest_pixels[k].x == nc->x &&
                                        closest_pixels[k].y == nc->y) {
                                        nc_already_in_closest_pixels = true;
                                    }
                                }
                                if (!nc_already_in_closest_pixels) {
                                    // Append nc to closest_pixels
                                    if (n_closest_pixels ==
                                        closest_pixels_buf_len) {
                                        closest_pixels_buf_len *= 2;
                                        void *temp =
                                            realloc(closest_pixels,
                                                    sizeof(closest_pixels[0]) *
                                                        closest_pixels_buf_len);
                                        if (temp == NULL) {
                                            for (int y = 0; y < height; y++)
                                                for (int x = 0; x < width; x++)
                                                    free(closest[y * width + x]
                                                             .coords);
                                            free(closest_pixels);
                                            free(closest);
                                            return false;
                                        }
                                        closest_pixels = temp;
                                    }
                                    closest_pixels[n_closest_pixels] = *nc;
                                    n_closest_pixels++;
                                }
                            }
                        }
                    }
                }
            }

            assert(n_closest_pixels != 0);

            closest[y * width + x].coords = closest_pixels;
            closest[y * width + x].n_coords = n_closest_pixels;

            // Take color from any one of the closest visible pixels
            IMACCESS(out_im, x, y) =
                IMACCESS(out_im, closest_pixels[0].x, closest_pixels[0].y);
        }
    }

    for (int y = 0; y < height; y++)
        for (int x = 0; x < width; x++)
            free(closest[y * width + x].coords);

    free(closest);

    // Update out_im to replace alpha=1 pixels with alpha=0 where alpha is 0 in
    // the input image
    // Use 0 as "uninitialized" since the image always has at least one color
    assert(tlut_count != 0);
    uint8_t tlut_map[256] = {0};
    for (int y = 0; y < height; y++) {
        for (int x = 0; x < width; x++) {
            if ((tlut[IMACCESS(im, x, y)] & 1) == 0) {
                if (tlut_map[IMACCESS(out_im, x, y)] == 0) {
                    tlut_map[IMACCESS(out_im, x, y)] = tlut_count;
                    out_tlut[tlut_count] =
                        out_tlut[IMACCESS(out_im, x, y)] & 0xFFFE;
                    tlut_count++;
                }
                IMACCESS(out_im, x, y) = tlut_map[IMACCESS(out_im, x, y)];
            }
        }
    }
    *out_tlut_count = tlut_count;

    return true;
}
