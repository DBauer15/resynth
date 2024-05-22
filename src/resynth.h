/*
    resynth - A program for resynthesizing textures.
    modified by David Bauer, 2024
    modified by Connor Olding, 2016
    Copyright (C) 2000 2008  Paul Francis Harrison
    Copyright (C) 2002  Laurent Despeyroux
    Copyright (C) 2002  David Rodríguez García

    This program is licensed under the terms of the GNU General Public
    License (version 2), and is distributed without any warranty.
    You should have received a copy of the license with the program.
    If not, visit <http://gnu.org/licenses/> to obtain one.
*/

#ifndef RESYNTH_H_DEFINED
#define RESYNTH_H_DEFINED
#include <stdlib.h> // for size_t
#include <stdint.h> // for uint8_t 
#include <stdbool.h> // we're targetting C11 anyway, may as well use it

#if __cplusplus
extern "C" {
#endif

struct _Resynth_state;
struct _Parameters;
struct _Resynth_result;
typedef struct _Resynth_state Resynth_state;
typedef struct _Parameters Parameters;
typedef struct _Resynth_result Resynth_result;

typedef Resynth_result* resynth_result_t;
typedef Resynth_state* resynth_state_t;
typedef Parameters* resynth_parameters_t;

/* Image and Buffer Loading */
resynth_state_t
resynth_state_create_from_image(const char* filename, int desired_channels, int scale);

resynth_state_t
resynth_state_create_from_memory(uint8_t* pixels, size_t width, size_t height, size_t channels, int scale);

resynth_state_t
resynth_state_create_from_memoryf(float* pixels, size_t width, size_t height, size_t channels, int scale);

/* Config */
resynth_parameters_t
resynth_parameters_create();

void
resynth_parameters_h_tile(resynth_parameters_t parameters, bool h_tile);

void
resynth_parameters_v_tile(resynth_parameters_t parameters, bool v_tile);

void
resynth_parameters_outlier_sensitivity(resynth_parameters_t parameters, double sensitivity);

void
resynth_parameters_neighbors(resynth_parameters_t parameters, int neighbors);

void
resynth_parameters_tries(resynth_parameters_t parameters, int tries);

void
resynth_parameters_magic(resynth_parameters_t parameters, int magic);

void
resynth_parameters_random_seed(resynth_parameters_t parameters, unsigned long seed);


/* Processing and Results */ 
resynth_result_t 
resynth_run(resynth_state_t state, resynth_parameters_t parameters);

bool 
resynth_result_valid(resynth_result_t result);

uint8_t* 
resynth_result_pixels(resynth_result_t result);

float* 
resynth_result_pixelsf(resynth_result_t result);

size_t
resynth_result_width(resynth_result_t result);

size_t
resynth_result_height(resynth_result_t result);

size_t
resynth_result_channels(resynth_result_t result);

/* Memory Management */ 
void
resynth_free_state(resynth_state_t state);

void
resynth_free_parameters(resynth_parameters_t parameters);

void
resynth_free_result(resynth_result_t result);

#if __cplusplus
}
#endif

#endif
