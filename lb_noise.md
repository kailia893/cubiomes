# Large-Biome Climate Noise Reference

This reference covers the large-biome Temperature, Humidity (`vegetation`), Continentalness, and Erosion noises. Weirdness is excluded.

## Example

The reusable [lb_noise.h](lb_noise.h) API and [lb_noise.c](lb_noise.c) implementation expose three operations: store a seed, sample one Perlin component, and sample a prefix containing the first N octave pairs. C has no member-function namespaces, so the short `lb_` prefix groups the API. The seed setter only stores the seed; noise is initialized when a sample is requested. It samples raw climate noise without the biome sampler's local coordinate shift. [lb_noise_demo.c](lb_noise_demo.c) checks the full prefix against Cubiomes' sampler.

Build and run it from the repository root:

```sh
cmake -S . -B build -DCMAKE_BUILD_TYPE=Release
cmake --build build --target cubiomes_static
gcc -I. examples/lb_noise_demo.c examples/lb_noise.c build/libcubiomes.a -O3 -Wall -Wextra -fwrapv -lm -o build/lb_noise_demo
./build/lb_noise_demo
```

Coordinates are biome coordinates (one sample per four horizontal blocks). `NP_TEMPERATURE`, `NP_HUMIDITY`, `NP_CONTINENTALNESS`, and `NP_EROSION` select the fields. `lb_octave()` takes a zero-based octave-pair index and component `'A'` or `'B'`. `lb_prefix()` sums complete pairs: `1` includes `0A + 0B`, and `2` includes `0A + 0B + 1A + 1B`. Invalid requests return `NAN`.

## Octave Tables

Each octave's expected spatial mean is zero; these four fields have no fixed DC offset. `Amplitude` is the configured octave weight before Double Perlin normalization. `Scale` multiplies input coordinates; `Source` is Cubiomes/Minecraft's signed octave number. `Tile` is the individual Perlin component's horizontal repeat length in biome coordinates. A components use `256 * Scale`; B components also stretch coordinates by `337/331`, giving `256 * Scale * 331/337`. Multiply tile lengths by four for block coordinates. Contributions are cumulative coefficient weights from `0A` through that row, not a guaranteed fraction of the sampled value at a position.

### Temperature

| Octave | Source | Mean | Amplitude | Scale | Tile | Cumulative contribution |
|---|---:|---:|---:|---:|---:|---:|
| 0A | -12 | 0 | 1.5 | 4096 | 1,048,576 | 42.857% |
| 0B | -12 | 0 | 1.5 | 4023.074 | 1,029,907 | 85.714% |
| 1A | -10 | 0 | 1 | 1024 | 262,144 | 92.857% |
| 1B | -10 | 0 | 1 | 1005.769 | 257,477 | 100.000% |

### Humidity

| Octave | Source | Mean | Amplitude | Scale | Tile | Cumulative contribution |
|---|---:|---:|---:|---:|---:|---:|
| 0A | -10 | 0 | 1 | 1024 | 262,144 | 33.333% |
| 0B | -10 | 0 | 1 | 1005.769 | 257,477 | 66.667% |
| 1A | -9 | 0 | 1 | 512 | 131,072 | 83.333% |
| 1B | -9 | 0 | 1 | 502.884 | 128,738 | 100.000% |

### Continentalness

| Octave | Source | Mean | Amplitude | Scale | Tile | Cumulative contribution |
|---|---:|---:|---:|---:|---:|---:|
| 0A | -11 | 0 | 1 | 2048 | 524,288 | 20.546% |
| 0B | -11 | 0 | 1 | 2011.537 | 514,953 | 41.091% |
| 1A | -10 | 0 | 1 | 1024 | 262,144 | 51.364% |
| 1B | -10 | 0 | 1 | 1005.769 | 257,477 | 61.637% |
| 2A | -9 | 0 | 2 | 512 | 131,072 | 71.910% |
| 2B | -9 | 0 | 2 | 502.884 | 128,738 | 82.183% |
| 3A | -8 | 0 | 2 | 256 | 65,536 | 87.319% |
| 3B | -8 | 0 | 2 | 251.442 | 64,369 | 92.456% |
| 4A | -7 | 0 | 2 | 128 | 32,768 | 95.024% |
| 4B | -7 | 0 | 2 | 125.721 | 32,185 | 97.592% |
| 5A | -6 | 0 | 1 | 64 | 16,384 | 98.234% |
| 5B | -6 | 0 | 1 | 62.861 | 16,092 | 98.876% |
| 6A | -5 | 0 | 1 | 32 | 8,192 | 99.197% |
| 6B | -5 | 0 | 1 | 31.430 | 8,046 | 99.518% |
| 7A | -4 | 0 | 1 | 16 | 4,096 | 99.679% |
| 7B | -4 | 0 | 1 | 15.715 | 4,023 | 99.839% |
| 8A | -3 | 0 | 1 | 8 | 2,048 | 99.920% |
| 8B | -3 | 0 | 1 | 7.858 | 2,012 | 100.000% |

### Erosion

| Octave | Source | Mean | Amplitude | Scale | Tile | Cumulative contribution |
|---|---:|---:|---:|---:|---:|---:|
| 0A | -11 | 0 | 1 | 2048 | 524,288 | 29.630% |
| 0B | -11 | 0 | 1 | 2011.537 | 514,953 | 59.259% |
| 1A | -10 | 0 | 1 | 1024 | 262,144 | 74.074% |
| 1B | -10 | 0 | 1 | 1005.769 | 257,477 | 88.889% |
| 2A | -8 | 0 | 1 | 256 | 65,536 | 92.593% |
| 2B | -8 | 0 | 1 | 251.442 | 64,369 | 96.296% |
| 3A | -7 | 0 | 1 | 128 | 32,768 | 98.148% |
| 3B | -7 | 0 | 1 | 125.721 | 32,185 | 100.000% |