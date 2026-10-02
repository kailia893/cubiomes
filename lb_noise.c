#include "lb_noise.h"

#include <math.h>
#include <string.h>

static const int octave_offsets[4] = {0, 4, 8, 26};
static const int octave_counts[4] = {2, 2, 9, 4};

void lb_setseed(LbNoise *n, uint64_t seed)
{
    n->seed = seed;
    n->seeded = 1;
    n->initialized = 0;
}

static int valid_parameter(int p)
{
    return p >= NP_TEMPERATURE && p <= NP_EROSION;
}

static void init_parameter(LbNoise *n, int p)
{
    BiomeNoise b = {0};
    DoublePerlinNoise *d;
    int offset = octave_offsets[p];
    int count;

    setClimateParaSeed(&b, n->seed, 1, p, -1);
    d = &b.climate[p];
    count = d->octA.octcnt + d->octB.octcnt;
    memcpy(n->octaves + offset, b.oct, count * sizeof(*n->octaves));
    n->climate[p] = *d;
    n->climate[p].octA.octaves = n->octaves + offset;
    n->climate[p].octB.octaves = n->octaves + offset + d->octA.octcnt;
    n->initialized |= 1u << p;
}

static int init(LbNoise *n, int p)
{
    if (!n->seeded || !valid_parameter(p))
        return 0;

    if (!(n->initialized & (1u << p)))
        init_parameter(n, p);
    return 1;
}

static int part(const DoublePerlinNoise *d, int o, char ab,
    double x, double z, double *v)
{
    const OctaveNoise *s;
    double f;
    const PerlinNoise *q;

    if (ab == 'A')
    {
        s = &d->octA;
        f = 1.0;
    }
    else if (ab == 'B')
    {
        s = &d->octB;
        f = 337.0 / 331.0;
    }
    else
        return 0;

    if (o < 0 || o >= s->octcnt)
        return 0;

    q = s->octaves + o;
    *v = d->amplitude * q->amplitude * samplePerlin(q,
        x * f * q->lacunarity, 0, z * f * q->lacunarity, 0, 0);
    return 1;
}

double lb_octave(LbNoise *n, int p, int o, char ab, double x, double z)
{
    double v;

    if (!n || !valid_parameter(p) || o < 0 || o >= octave_counts[p] ||
        (ab != 'A' && ab != 'B'))
        return NAN;
    if (!init(n, p) || !part(&n->climate[p], o, ab, x, z, &v))
        return NAN;
    return v;
}

int lb_octave_int(LbNoise *n, int p, int o, char ab, double x, double z){
    return (int)(lb_octave(n, p, o, ab, x/4, z/4) * 10000);
}

int lb_octave_prefix_sum(LbNoise *n, int p, int o, double x, double z){
    int sum = 0;
    int idx = 0;
    for(int i = 0; i < o; i++){
        if (idx == 0) {
            sum += lb_octave_int(n, p, i/2, 'A', x, z);
            idx = 1;
        }
        else {
            sum += lb_octave_int(n, p, i/2, 'B', x, z);
            idx = 0;
        }

    }
    return sum;
}

uint64_t to_unsigned(int64_t seed)
{
    return (uint64_t)(seed);
}