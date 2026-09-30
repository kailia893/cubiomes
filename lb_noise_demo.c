#include "lb_noise.h"
//Compile: gcc lb_noise_demo.c lb_noise.c libcubiomes.a -lm -o test
#include <math.h>
#include <stdio.h>
static const int ps[] = {
    NP_TEMPERATURE, NP_HUMIDITY, NP_CONTINENTALNESS, NP_EROSION
};
static const char *names[] = {
    "Temperature", "Humidity", "Continentalness", "Erosion"
};
int main(void)
{
    LbNoise n = {0};
    uint64_t seed = to_unsigned((-8817339506320444920LL));
    double x = 0, z = 0;
    int i;

    lb_setseed(&n, seed);
    printf("Humidity full: %d\n", lb_octave_prefix_sum(&n, NP_HUMIDITY, 4, x, z));
    
}