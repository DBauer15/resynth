#include <stdio.h>
#include <stdbool.h>
#include <string.h>
#include <limits.h>
#include <resynth.h>

// for command-line argument parsing
#include "kyaa.h"
#include "kyaa_extra.h"

// likewise for stb_image_write. by using the static keyword,
// any unused formats and functions can be stripped from the resulting binary.
// we only use png (stbi_write_png) in this case.
#define STB_IMAGE_WRITE_IMPLEMENTATION
#define STB_IMAGE_WRITE_STATIC
#pragma GCC diagnostic push
#pragma GCC diagnostic ignored "-Wmissing-prototypes"
#include "stb_image_write.h"
#pragma GCC diagnostic pop

static char *manipulate_filename(const char *fn,
                                 const char *new_extension) {
#define MAX_LENGTH 256
    int length = strlen(fn);
    if (length > MAX_LENGTH) length = MAX_LENGTH;
    // out_fn must be freed by the caller.
    char *out_fn = (char *)calloc(2 * MAX_LENGTH, 1);
    strncpy(out_fn, fn, length);

    char *hint = strrchr(out_fn, '.');
    if (hint == NULL) strcpy(out_fn + length, new_extension);
    else strcpy(hint, new_extension);

    return out_fn;
#undef MAX_LENGTH
}


int main(int argc, char** argv) {
    int ret = 0;
    int scale = 1;
    int autism = 32;
    int neighbors = 29;
    int tries = 192;
    int magic = 192;
    unsigned long seed = 0;

    KYAA_LOOP {
        KYAA_BEGIN

        KYAA_FLAG_LONG('a', "autism",
"        sensitivity to outliers\n"
"        range: [0,256];     default: 32")
            autism = (double)(kyaa_long_value) / 256.;

        KYAA_FLAG_LONG('N', "neighbors",
"        points to use when sampling\n"
"        range: [0,1024];    default: 29")
            neighbors = kyaa_long_value;

        KYAA_FLAG_LONG('M', "tries",
"        random points added to candidates\n"
"        range: [0,65536];   default: 192")
            tries = kyaa_long_value;

        KYAA_FLAG_LONG('m', "magic",
"        magic constant, affects iterations\n"
"        range: [0,255];     default: 192")
            magic = kyaa_long_value;

        KYAA_FLAG_LONG('s', "scale",
"        output size multiplier; negative values set width and height\n"
"        range: [-8192,32];  default: 1")
            scale = kyaa_long_value;

        KYAA_FLAG_LONG('S', "seed",
"        initial RNG value\n"
"                            default: 0 [time(0)]")
            seed = (unsigned long) kyaa_long_value;

        KYAA_HELP("  {files...}\n"
"        image files to open, resynthesize, and save as {filename}.resynth.png\n"
"        required            default: [none]")

        KYAA_END

        if (kyaa_read_stdin) {
            fprintf(stderr, "fatal error: reading from stdin is unsupported\n");
            exit(1);
        }

        const char *fn = kyaa_arg;

        resynth_state_t state = resynth_state_create_from_image(fn, 3, 1);
        resynth_parameters_t params = resynth_parameters_create();
        resynth_parameters_outlier_sensitivity(params, autism);
        resynth_parameters_neighbors(params, neighbors);
        resynth_parameters_magic(params, magic);
        resynth_parameters_tries(params, tries);
        resynth_parameters_random_seed(params, seed);

        resynth_result_t result = resynth_run(state, params);

	printf("Channels %d", resynth_result_channels(result));

        char *out_fn = manipulate_filename(fn, ".resynth.png");
        puts(out_fn);
        int write_result = stbi_write_png(out_fn, 
                                    resynth_result_width(result), 
                                    resynth_result_height(result), 
                                    resynth_result_channels(result), 
                                    resynth_result_pixels(result), 
                                    0);
        if (!write_result) {
            fprintf(stderr, "failed to write: %s\n", out_fn);
            ret--;
        }

        free(out_fn);
        resynth_free_result(result);
        resynth_free_parameters(params);
        resynth_free_state(state);
    }

    return ret;
}
