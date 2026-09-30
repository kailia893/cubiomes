#ifndef LB_NOISE_H
#define LB_NOISE_H

#include "biomenoise.h"

typedef struct
{
    uint64_t seed;
    int seeded;
    unsigned int initialized;
    DoublePerlinNoise climate[4];
    PerlinNoise octaves[2 * (2 + 2 + 9 + 4)];
} LbNoise;

void lb_setseed(LbNoise *n, uint64_t seed);
double lb_octave(LbNoise *n, int p, int o, char ab, double x, double z);
int lb_octave_int(LbNoise *n, int p, int o, char ab, double x, double z);
int lb_octave_prefix_sum(LbNoise *n, int p, int o, double x, double z);
uint64_t to_unsigned(int64_t seed);

#endif