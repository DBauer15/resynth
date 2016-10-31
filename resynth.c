/*
    resynth - A program for resynthesizing textures.
    modified by Connor Olding, 2016
    Copyright (C) 2000 2008  Paul Francis Harrison
    Copyright (C) 2002  Laurent Despeyroux
    Copyright (C) 2002  David Rodríguez García

    This program is licensed under the terms of the GNU General Public
    License (version 2), and is distributed without any warranty.
    You should have received a copy of the license with the program.
    If not, visit <http://gnu.org/licenses/> to obtain one.
*/

#include <errno.h>
#include <math.h>
#include <stdbool.h>
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <time.h>

#define STB_IMAGE_IMPLEMENTATION
#define STB_IMAGE_STATIC
#define STBI_ONLY_JPEG
#define STBI_ONLY_PNG
#define STBI_ONLY_BMP
#define STBI_ONLY_GIF
#include "stb_image.h"

#define STB_IMAGE_WRITE_IMPLEMENTATION
#define STB_IMAGE_WRITE_STATIC
#include "stb_image_write.h"

#define STRETCHY_BUFFER_OUT_OF_MEMORY \
    fprintf(stderr, "fatal error: ran out of memory in stb__sbgrowf\n"); \
    exit(1);
#include "stretchy_buffer.h"

#include "kyaa.h"

#define MIN(a, b) ((a) < (b) ? (a) : (b))
#define MAX(a, b) ((a) > (b) ? (a) : (b))
#define CLAMP(x, l, u) (MIN(MAX(x, l), u))
#define CLAMPV(x, l, u) x = CLAMP(x, l, u)
#define LEN(a) (sizeof(a) / sizeof((a)[0]))

#define MEMORY(a, size) \
    do { \
        if (a) (a) = (free(a), NULL); \
        if (size > 0) (a) = (typeof(a))(calloc(size, sizeof((a)[0]))); \
    } while (0) \

#define sb_freeset(a) ((a) = (sb_free(a), NULL))

#define INLINE static inline

// end of generic boilerplate, here's the actual program:

typedef struct coord {
    int x, y;
} Coord;

INLINE Coord coord_add(const Coord a, const Coord b) {
    return (Coord){a.x + b.x, a.y + b.y};
}

INLINE Coord coord_sub(const Coord a, const Coord b) {
    return (Coord){a.x - b.x, a.y - b.y};
}

static int coord_compare(const void *v_a, const void *v_b) {
    const Coord *a = (Coord *) v_a;
    const Coord *b = (Coord *) v_b;
    return (a->x * a->x + a->y * a->y) - (b->x * b->x + b->y * b->y);
}

typedef uint8_t Pixel;

typedef union {
    Pixel v[4];
    struct {
        Pixel r, g, b, a;
    };
} Pixel32;

typedef struct {
    bool has_value, has_source;
    Coord source;
} Status;

typedef struct {
    int width, height, depth;
} Image;

#define IMAGE_RESIZE(image, w, h, d) \
    do { \
        image.width = w; \
        image.height = h; \
        image.depth = d; \
        MEMORY(image##_array, w * h * d); \
    } while (0) \

#define image_at(image, x, y) (image##_array + (y * image.width + x) * image.depth)
#define image_atc(image, coord) image_at(image, coord.x, coord.y)

typedef struct {
    bool h_tile, v_tile;
    double autism;
    int neighbors, tries;
    int polish, magic;
} Parameters;

INLINE bool wrap_or_clip(const Parameters parameters, const Image image,
                         Coord *point) {
    while (point->x < 0) {
        if (parameters.h_tile) point->x += image.width;
        else return false;
    }
    while (point->x >= image.width) {
        if (parameters.h_tile) point->x -= image.width;
        else return false;
    }
    while (point->y < 0) {
        if (parameters.v_tile) point->y += image.height;
        else return false;
    }
    while (point->y >= image.height) {
        if (parameters.v_tile) point->y -= image.height;
        else return false;
    }
    return true;
}

typedef struct {
    int input_bytes;
    Image data, corpus, status;
    Pixel *data_array, *corpus_array;
    Status *status_array;
    Coord *data_points, *corpus_points, *sorted_offsets;
    Image tried;
    int *tried_array;

    Coord *neighbors;
    Pixel32 *neighbor_values;
    Status **neighbor_statuses;
    int n_neighbors;

    int *diff_table; // uint16_t ?

    int best;
    Coord best_point;
} Resynth_state;

static void state_free(Resynth_state *s) {
    sb_freeset(s->data_points);
    sb_freeset(s->corpus_points);
    sb_freeset(s->sorted_offsets);
    MEMORY(s->diff_table, 0);
    MEMORY(s->data_array, 0);
    MEMORY(s->corpus_array, 0);
    MEMORY(s->status_array, 0);
    MEMORY(s->tried_array, 0);
}

static double neglog_cauchy(double x) {
    return log(x * x + 1.0);
}

static void make_offset_list(Resynth_state *s) {
    int width = MIN(s->corpus.width, s->data.width);
    int height = MIN(s->corpus.height, s->data.height);

    sb_freeset(s->sorted_offsets);
    for (int y = -height + 1; y < height; y++) {
        for (int x = -width + 1; x < width; x++) {
            Coord c = {x, y};
            sb_push(s->sorted_offsets, c);
        }
    }

    qsort(s->sorted_offsets, sb_count(s->sorted_offsets),
          sizeof(Coord), coord_compare);
}

INLINE void try_point(Resynth_state *s, const Coord point) {
    int sum = 0;

    for (int i = 0; i < s->n_neighbors; i++) {
        Coord off_point = coord_add(point, s->neighbors[i]);
        if (off_point.x < 0 || off_point.y < 0 ||
            off_point.x >= s->corpus.width || off_point.y >= s->corpus.height) {
            sum += s->diff_table[0] * s->input_bytes;
        } else if (i) {
            const Pixel *corpus_pixel = image_atc(s->corpus, off_point);
            const Pixel *data_pixel = s->neighbor_values[i].v;
            for (int j = 0; j < s->input_bytes; j++) {
                sum += s->diff_table[256 + data_pixel[j] - corpus_pixel[j]];
            }
        }

        if (sum >= s->best) return;
    }

    s->best = sum;
    s->best_point = point;
}

static void run(Resynth_state *s, Parameters parameters) {
    sb_freeset(s->data_points);
    sb_freeset(s->corpus_points);
    sb_freeset(s->sorted_offsets);

    MEMORY(s->diff_table, 512);
    MEMORY(s->neighbors, parameters.neighbors);
    MEMORY(s->neighbor_values, parameters.neighbors);
    MEMORY(s->neighbor_statuses, parameters.neighbors);

    IMAGE_RESIZE(s->status, s->data.width, s->data.height, 1);

    for (int y = 0; y < s->status.height; y++) {
        for (int x = 0; x < s->status.width; x++) {
            image_at(s->status, x, y)->has_source = false;
            image_at(s->status, x, y)->has_value = false;
            Coord coord = {x, y};
            sb_push(s->data_points, coord);
        }
    }

    for (int y = 0; y < s->corpus.height; y++) {
        for (int x = 0; x < s->corpus.width; x++) {
            Coord coord = {x, y};
            sb_push(s->corpus_points, coord);
        }
    }

    if (!sb_count(s->corpus_points) || !sb_count(s->data_points)) {
        fprintf(stderr, "invalid sizes\n");
        fprintf(stderr, "corpus: %i\n", sb_count(s->corpus_points));
        fprintf(stderr, "data: %i\n", sb_count(s->data_points));
        return;
    }

    make_offset_list(s);

    if (parameters.autism > 0) for (int i = -256; i < 256; i++) {
        double value = neglog_cauchy(i / 256.0 / parameters.autism) /
                       neglog_cauchy(1.0 / parameters.autism) * 65536.0;
        s->diff_table[256 + i] = (int)(value);
    } else for (int i = -256; i < 256; i++) {
        s->diff_table[256 + i] = (int)(i != 0) * 65536;
    }

    int data_area = sb_count(s->data_points);

    for (int p = 0; p < parameters.polish + p; p++) {
        for (int i = 0; i < data_area; i++) {
            // shuffle
            int j = rand() % data_area;
            Coord temp = s->data_points[i];
            s->data_points[i] = s->data_points[j];
            s->data_points[j] = temp;
        }

        if (parameters.magic) for (int n = data_area; n > 0;) {
            n = n * parameters.magic / 256;
            for (int i = 0; i < n; i++) {
                sb_push(s->data_points, s->data_points[i]);
            }
        }
    }

    IMAGE_RESIZE(s->tried, s->corpus.width, s->corpus.height, 1);
    int corpus_area = s->corpus.width * s->corpus.height;
    for (int i = 0; i < corpus_area; i++) s->tried_array[i] = -1;

    // finally, do it

    for (int i = sb_count(s->data_points) - 1; i >= 0; i--) {
        Coord position = s->data_points[i];

        // this point will always have a value by the end of this iteration
        image_atc(s->status, position)->has_value = true;

        s->n_neighbors = 0;
        const int sorted_offsets_size = sb_count(s->sorted_offsets);
        for (int j = 0; j < sorted_offsets_size; j++) {
            Coord point = coord_add(position, s->sorted_offsets[j]);

            if (wrap_or_clip(parameters, s->data, &point) &&
                image_atc(s->status, point)->has_value) {
                s->neighbors[s->n_neighbors] = s->sorted_offsets[j];
                s->neighbor_statuses[s->n_neighbors] =
                    image_atc(s->status, point);
                for (int k = 0; k < s->input_bytes; k++) {
                    s->neighbor_values[s->n_neighbors].v[k] =
                        image_atc(s->data, point)[k];
                }
                s->n_neighbors++;
                if (s->n_neighbors >= parameters.neighbors) break;
            }
        }

        s->best = __INT_MAX__;

        for (int j = 0; j < s->n_neighbors && s->best != 0; j++) {
            if (s->neighbor_statuses[j]->has_source) {
                Coord point = coord_sub(s->neighbor_statuses[j]->source,
                                        s->neighbors[j]);
                if (point.x < 0 || point.y < 0 ||
                    point.x >= s->corpus.width || point.y >= s->corpus.height) {
                    continue;
                }
                if (*image_atc(s->tried, point) == i) continue;
                try_point(s, point);
                *image_atc(s->tried, point) = i;
            }
        }

        for (int j = 0; j < parameters.tries && s->best != 0; j++) {
            try_point(s, s->corpus_points[rand() % sb_count(s->corpus_points)]);
        }

        for (int j = 0; j < s->input_bytes; j++) {
            image_atc(s->data, position)[j] =
                image_atc(s->corpus, s->best_point)[j];
        }
        image_atc(s->status, position)->has_source = true;
        image_atc(s->status, position)->source = s->best_point;
    }
}

static char *manipulate_filename(const char *fn,
                                 const char *new_extension) {
#define MAX_LENGTH 256
    int length = strlen(fn);
    if (length > MAX_LENGTH)
        length = MAX_LENGTH;
    char *out_fn = (char *)calloc(2 * MAX_LENGTH, 1);
    strncpy(out_fn, fn, length);

    char *hint = strrchr(out_fn, '.');
    if (hint == NULL) strcpy(out_fn + length, new_extension);
    else strcpy(hint, new_extension);

    return out_fn;
#undef MAX_LENGTH
}

static const int disc00[] = {
    // http://oeis.org/A057961
    1,    5,    9,    13,   21,   25,   29,   37,
    45,   49,   57,   61,   69,   81,   89,   97,
    101,  109,  113,  121,  129,  137,  145,  149,
    161,  169,  177,  185,  193,  197,  213,  221,
    225,  233,  241,  249,  253,  261,  277,  285,
    293,  301,  305,  317,  325,  333,  341,  349,
    357,  365,  373,  377,  385,  401,  405,  421,
    429,  437,  441,  457,  465,  473,  481,  489,
    497,  505,  509,  517,  529,  545,  553,  561,
    569,  577,  593,  601,  609,  613,  621,  633,
    641,  657,  665,  673,  681,  697,  709,  717,
    725,  733,  741,  749,  757,  761,  769,  777,
    793,  797,  805,  821,  829,  845,  853,  861,
    869,  877,  885,  889,  901,  917,  925,  933,
    941,  949,  965,  973,  981,  989,  997,  1005,
    1009, 1033, 1041, 1049, 1057, 1069, 1085, 1093
};

int main(int argc, char *argv[]) {
    Resynth_state state = {0};
    Resynth_state *s = &state; // just for consistency

    Parameters parameters = {0};
    parameters.v_tile = true;
    parameters.h_tile = true;
    parameters.magic = 192;         // 192 (3/4)
    parameters.autism = 32. / 256.; // 30. / 256.
    parameters.neighbors = 29;      // 30
    parameters.tries = 192;         // 200 (or 80 in the paper)
    parameters.polish = 0;          // 0

    int scale = 1;
    unsigned long seed = 0;

    int ret = 0;

    KYAA_LOOP {
        KYAA_BEGIN

        KYAA_FLAG_LONG('a', "autism",
"        sensitivity to outliers\n"
"        range: [0,256];     default: 32")
            parameters.autism = (double)(kyaa_flag_arg) / 256.;

        KYAA_FLAG_LONG('N', "neighbors",
"        points to use when sampling\n"
"        range: [0,1024];    default: 29")
            parameters.neighbors = kyaa_flag_arg;

        KYAA_FLAG_LONG('r', "radius",
"        square neighborhood, always odd\n"
"        range: [0,32];      default: [n/a]")
            int radius = kyaa_flag_arg;
            radius = 2 * MAX(radius, 0) + 1;
            parameters.neighbors = radius * radius;

        KYAA_FLAG_LONG('R', "circle-radius",
"        circle neighborhood radius\n"
"        range: [1,128];     default: [n/a]")
            int radius = kyaa_flag_arg;
            radius = CLAMP(radius, 1, (int)(LEN(disc00)));
            parameters.neighbors = disc00[radius - 1];

        KYAA_FLAG_LONG('M', "tries",
"        random points added to candidates\n"
"        range: [0,65536];   default: 192")
            parameters.tries = kyaa_flag_arg;

        KYAA_FLAG_LONG('p', "polish",
"        extra iterations\n"
"        range: [0,9];       default: 0")
            parameters.polish = kyaa_flag_arg;

        KYAA_FLAG_LONG('m', "magic",
"        magic constant, affects iterations\n"
"        range: [0,255];     default: 192")
            parameters.magic = kyaa_flag_arg;

        KYAA_FLAG_LONG('s', "scale",
"        output size multiplier; negative values set width and height\n"
"        range: [-8192,32];  default: 1")
            scale = kyaa_flag_arg;

        KYAA_FLAG_LONG('S', "seed",
"        initial RNG value\n"
"                            default: 0 [time(0)]")
            seed = (unsigned long) kyaa_flag_arg;

        KYAA_HELP("  {files...}\n"
"        image files to open, resynthesize, and save as {filename}.resynth.png\n"
"        required            default: [none]")

        KYAA_END

        if (kyaa_read_stdin) {
            fprintf(stderr, "fatal error: reading from stdin is unsupported\n");
            exit(1);
        }

        CLAMPV(parameters.polish, 0, 9);
        CLAMPV(parameters.magic, 0, 255);
        CLAMPV(parameters.autism, 0., 1.);
        CLAMPV(parameters.neighbors, 0, disc00[LEN(disc00) - 1]);
        CLAMPV(parameters.tries, 0, 65536);
        CLAMPV(scale, -8192, 32);

        char *fn = kyaa_arg;

        int w, h, d;
        uint8_t *image = stbi_load(fn, &w, &h, &d, 0);
        if (image == NULL) {
            fprintf(stderr, "invalid image: %s\n", fn);
            ret--;
            continue;
        }

        IMAGE_RESIZE(s->corpus, w, h, d);
        memcpy(s->corpus_array, image, w * h * d);

        s->input_bytes = MIN(d, 3);

        {
            int data_w = 256, data_h = 256;
            if (scale > 0) data_w = scale * w, data_h = scale * h;
            if (scale < 0) data_w = -scale, data_h = -scale;
            IMAGE_RESIZE(s->data, data_w, data_h, s->input_bytes);
        }

        stbi_image_free(image);

        if (seed) srand(seed);
        else srand(time(0));
        run(s, parameters);

        char *out_fn = manipulate_filename(fn, ".resynth.png");
        puts(out_fn);
        int result = stbi_write_png(out_fn, s->data.width, s->data.height,
                                    s->data.depth, s->data_array, 0);
        if (!result) {
            fprintf(stderr, "failed to write: %s\n", out_fn);
            ret--;
        }

        free(out_fn);
    }

    state_free(s);

    return ret;
}