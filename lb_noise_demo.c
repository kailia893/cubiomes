#include "lb_noise.h"
//Compile: gcc -O3 -fwrapv lb_noise_demo.c lb_noise.c libcubiomes.a -pthread -lm -o test
#include <errno.h>
#include <inttypes.h>
#include <limits.h>
#include <pthread.h>
#include <stdatomic.h>
#include <math.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <time.h>
static const int ps[] = {
    NP_TEMPERATURE, NP_HUMIDITY, NP_CONTINENTALNESS, NP_EROSION
};
static const char *names[] = {
    "Temperature", "Humidity", "Continentalness", "Erosion"
};
//block positions, not chunk
int positions[5][2] = {
    {-1000, -1000},
    {1000, 1000},
    {-1000, 1000},
    {1000, -1000},
    {0, 0}
};
int tile1[4][2] = {
    {0, 0},
    {1048576, 0},
    {0, 1048576},
    {1048576, 1048576}
};
int tile2[4][4][2] = {
    {{0, 0}, {2097152, 0}, {0, 2097152}, {2097152, 2097152}},
    {{1048576, 0}, {3145728, 0}, {1048576, 2097152}, {3145728, 2097152}},
    {{0, 1048576}, {2097152, 1048576}, {0, 3145728}, {2097152, 3145728}},
    {{1048576, 1048576}, {3145728, 1048576}, {1048576, 3145728}, {3145728, 3145728}}
};
int templimit[6][2] = {
    {0,4000},
    {4000,0},
    {4000,4000},
    {0,5000},
    {5000,0},
    {5000,5000}
};
int multipliers[4][2] = {
    {1,1},
    {1,-1},
    {-1,1},
    {-1,-1}
};
static pthread_mutex_t output_mutex = PTHREAD_MUTEX_INITIALIZER;
static int progress_line_active;

#define FLOOD_QUEUE_CAPACITY 20000
#define FLOOD_HASH_CAPACITY 65536

typedef struct {
    int queue[FLOOD_QUEUE_CAPACITY][2];
    uint64_t keys[FLOOD_HASH_CAPACITY];
    uint32_t generations[FLOOD_HASH_CAPACITY];
    uint32_t generation;
} FloodFillScratch;

static int floodfill_mark(FloodFillScratch *scratch, int x, int z)
{
    uint64_t key = ((uint64_t)(uint32_t)x << 32) | (uint32_t)z;
    uint64_t hash = key;
    size_t slot;

    hash ^= hash >> 33;
    hash *= UINT64_C(0xff51afd7ed558ccd);
    hash ^= hash >> 33;
    hash *= UINT64_C(0xc4ceb9fe1a85ec53);
    hash ^= hash >> 33;
    slot = (size_t)hash & (FLOOD_HASH_CAPACITY - 1);
    while (scratch->generations[slot] == scratch->generation) {
        if (scratch->keys[slot] == key)
            return 0;
        slot = (slot + 1) & (FLOOD_HASH_CAPACITY - 1);
    }
    scratch->generations[slot] = scratch->generation;
    scratch->keys[slot] = key;
    return 1;
}

int check_candidate_seed(LbNoise *n, int64_t x, int64_t z,
    FloodFillScratch *scratch, const int erosion_order[4], int *cells_visited) {
    //Early filters
    for (int i = 0; i < 4; i++) {
        int position = erosion_order[i];
        if (lb_octave_prefix_sum(n, NP_EROSION, 4, 
            x + positions[position][0], z + positions[position][1]) > -4000) return 0;
    }
    for (int i = 0; i < 4; i++) {
        if (lb_octave_prefix_sum(n, NP_HUMIDITY, 2, 
            x + positions[i][0], z + positions[i][1]) < 1000) return 1;
    }
    for (int i = 0; i < 4; i++) {
        if (lb_octave_prefix_sum(n, NP_TEMPERATURE, 4, 
            x + positions[i][0], z + positions[i][1]) < 5500) return 2;
    }
    for (int i = 0; i < 4; i++) {
        if (lb_octave_prefix_sum(n, NP_CONTINENTALNESS, 2, 
            x + positions[i][0], z + positions[i][1]) < 0) return 3;
    }
    if (lb_octave_prefix_sum(n, NP_TEMPERATURE, 4, x, z) < 5500) return 4;
    for (int i = 0; i < 6; i++) {
        for (int j = 0; j < 4; j++) {
            if (lb_octave_prefix_sum(n, NP_TEMPERATURE, 4, 
                x + templimit[i][0] * multipliers[j][0], 
                z + templimit[i][1] * multipliers[j][1]) > 5500) return 5;
        }
    }
    //Floodfill
    if (++scratch->generation == 0) {
        memset(scratch->generations, 0, sizeof(scratch->generations));
        scratch->generation = 1;
    }
    int (*queue)[2] = scratch->queue;
    queue[0][0] = (int)x;
    queue[0][1] = (int)z;
    floodfill_mark(scratch, queue[0][0], queue[0][1]);
    int head = 0;
    int tail = 1;
    int visited = 0;
    while (head < tail) {
        int cx = queue[head][0];
        int cz = queue[head][1];
        head++;
        int temperature = lb_octave_prefix_sum(n, NP_TEMPERATURE, 4, cx, cz);
        if (temperature < 5500) {
            continue;
        }
        int humidity = lb_octave_prefix_sum(n, NP_HUMIDITY, 4, cx, cz);
        if (humidity < 1000)
            return 6;
        int erosion = lb_octave_prefix_sum(n, NP_EROSION, 6, cx, cz);
        if (erosion > -4000)
            return 6;
        int continental = lb_octave_prefix_sum(n, NP_CONTINENTALNESS, 8, cx, cz);
        if (continental < 0)
            return 6;
        visited++;
        int dx[4] = {256, -256, 0, 0};
        int dz[4] = {0, 0, 256, -256};
        for (int dir = 0; dir < 4; dir++) {
            int nx = cx + dx[dir];
            int nz = cz + dz[dir];
            if (!floodfill_mark(scratch, nx, nz)) {
                continue;
            }
            if (tail >= FLOOD_QUEUE_CAPACITY) {
                return 6;
            }
            queue[tail][0] = nx;
            queue[tail][1] = nz;
            tail++;
        }
    }
    *cells_visited = visited;
    return 65535 * visited;
}
typedef struct {
    uint64_t start_seed_bits;
    uint64_t seed_count;
    FILE *results_file;
    atomic_uint_fast64_t *completed_seeds;
    atomic_uint *finished_threads;
} WorkerArgs;

static double elapsed_seconds(struct timespec start, struct timespec end)
{
    return end.tv_sec - start.tv_sec +
        (end.tv_nsec - start.tv_nsec) / 1000000000.0;
}

static int parse_positive_u64(const char *text, uint64_t *value)
{
    char *end;
    unsigned long long parsed;

    if (!text || !*text || *text == '-')
        return 0;
    errno = 0;
    parsed = strtoull(text, &end, 10);
    if (errno || *end || parsed == 0 || parsed > INT64_MAX)
        return 0;
    *value = (uint64_t)parsed;
    return 1;
}

static int parse_i64(const char *text, int64_t *value)
{
    char *end;
    intmax_t parsed;

    if (!text || !*text)
        return 0;
    errno = 0;
    parsed = strtoimax(text, &end, 10);
    if (errno || *end || parsed < INT64_MIN || parsed > INT64_MAX)
        return 0;
    *value = (int64_t)parsed;
    return 1;
}

static int64_t seed_from_bits(uint64_t bits)
{
    const uint64_t sign_bit = UINT64_C(1) << 63;

    if (bits < sign_bit)
        return (int64_t)bits;
    return INT64_MIN + (int64_t)(bits - sign_bit);
}

static void *worker_thread(void *data) {
    WorkerArgs *args = data;
    LbNoise n = {0};
    FloodFillScratch scratch = {0};
    for (uint64_t offset = 0; offset < args->seed_count; offset++) {
        int64_t i = seed_from_bits(args->start_seed_bits + offset);
        lb_setseed(&n, to_unsigned(i));
        int bad = 0;
        for (int j = 0; j < 5; j++) {
            if (lb_octave_int(&n, NP_HUMIDITY, 0, 'A', 
                positions[j][0], positions[j][1]) < -1500) bad = 1;
            if(bad) break;
        }
        if (bad) {
            atomic_fetch_add_explicit(args->completed_seeds, 1, memory_order_relaxed);
            continue;
        }
        // H0A tile -> C0A & E0A tile (*2) (1048576 -> 2097152)
        for (int j = 0; j < 4; j++) {
            int x = tile1[j][0];
            int z = tile1[j][1];
            int notbad = 1;
            int erosion_a0[5];
            int erosion_order[4] = {0, 1, 2, 3};
            for (int k = 0; k < 5; k++) {
                erosion_a0[k] = lb_octave_int(&n, NP_EROSION, 0, 'A',
                    x + positions[k][0], z + positions[k][1]);
                if (erosion_a0[k] > 0) notbad = 0;
                if (!notbad) break;
            }
            if(!notbad) continue;
            for (int k = 0; k < 4; k++) {
                for (int l = k + 1; l < 4; l++) {
                    if (erosion_a0[erosion_order[l]] > erosion_a0[erosion_order[k]]) {
                        int swap = erosion_order[k];
                        erosion_order[k] = erosion_order[l];
                        erosion_order[l] = swap;
                    }
                }
            }
            for (int k = 0; k < 5; k++) {
                if (lb_octave_int(&n, NP_CONTINENTALNESS, 0, 'A', 
                    x + positions[k][0], z + positions[k][1]
                    ) < -2000) notbad = 0;
                if (!notbad) break;
            }
            if(!notbad) continue;
            // C0A & E0A -> T0A tile (*2) (2097152 -> 4194304)
            for (int k = 0; k < 4; k++) {
                notbad = 1;
                int x2 = tile2[j][k][0];
                int z2 = tile2[j][k][1];
                for (int l = 0; l < 5; l++) {
                    if (lb_octave_int(&n, NP_TEMPERATURE, 0, 'A', 
                        x2 + positions[l][0], z2 + positions[l][1]
                        ) < -500) notbad = 0;
                    if (!notbad) break;
                }
                if(!notbad) continue;
                //printf("Seed: %ld, Tile: (%d, %d), Subtile: (%d, %d)\n", i, x, z, x2, z2);
                for (int shiftx = (-4194304)*165; shiftx <= (4194304)*165; shiftx += 4194304) {
                    for (int shiftz = (-4194304)*165; shiftz <= (4194304)*165; shiftz += 4194304) {
                        int cells_visited = 0;
                        int score = check_candidate_seed(&n, x2+shiftx, z2+shiftz,
                            &scratch, erosion_order, &cells_visited);
                        if (score > 6) {
                            pthread_mutex_lock(&output_mutex);
                            if (progress_line_active)
                                fputc('\n', stderr);
                            fprintf(stderr, "[GOT RESULT] seed=%" PRId64
                                ", start=(%d, %d), block_size=%d, cells_visited=%d\n",
                                to_unsigned(i), x2+shiftx, z2+shiftz,
                                score, cells_visited);
                            fprintf(args->results_file, "[GOT RESULT] seed=%" PRId64
                                ", start=(%d, %d), block_size=%d, cells_visited=%d\n",
                                to_unsigned(i), x2+shiftx, z2+shiftz,
                                score, cells_visited);
                            fflush(args->results_file);
                            progress_line_active = 0;
                            pthread_mutex_unlock(&output_mutex);
                        }
                    }
                }
            }
        }
        atomic_fetch_add_explicit(args->completed_seeds, 1, memory_order_relaxed);
    }
    atomic_fetch_add_explicit(args->finished_threads, 1, memory_order_relaxed);
    return NULL;
}

int main(int argc, char **argv)
{
    int64_t first_seed = 0;
    uint64_t total_seeds = 10000;
    uint64_t requested_threads = 4;
    uint64_t thread_count;
    uint64_t next_offset = 0;
    uint64_t last_reported = 0;
    atomic_uint_fast64_t completed_seeds = 0;
    atomic_uint finished_threads = 0;
    pthread_t *threads;
    WorkerArgs *args;
    size_t started = 0;
    struct timespec start, now;

    if (argc > 4 || (argc > 1 && !parse_i64(argv[1], &first_seed)) ||
        (argc > 2 && !parse_positive_u64(argv[2], &total_seeds)) ||
        (argc > 3 && !parse_positive_u64(argv[3], &requested_threads))) {
        fprintf(stderr, "Usage: %s [l] [seeds] [threads]\n", argv[0]);
        return EXIT_FAILURE;
    }
    thread_count = requested_threads < total_seeds ? requested_threads : total_seeds;
    if (thread_count > INT_MAX ||
        thread_count > SIZE_MAX / sizeof(*threads) ||
        thread_count > SIZE_MAX / sizeof(*args)) {
        fprintf(stderr, "Requested thread count is too large\n");
        return EXIT_FAILURE;
    }
    threads = calloc((size_t)thread_count, sizeof(*threads));
    args = calloc((size_t)thread_count, sizeof(*args));
    if (!threads || !args) {
        fprintf(stderr, "Unable to allocate thread state\n");
        free(threads);
        free(args);
        return EXIT_FAILURE;
    }
    FILE *results_file = fopen("results.txt", "w");
    if (!results_file) {
        perror("results.txt");
        free(threads);
        free(args);
        return EXIT_FAILURE;
    }

    clock_gettime(CLOCK_MONOTONIC, &start);
    for (uint64_t i = 0; i < thread_count; i++) {
        uint64_t count = total_seeds / thread_count + (i < total_seeds % thread_count);
        args[i].start_seed_bits = (uint64_t)first_seed + next_offset;
        args[i].seed_count = count;
        args[i].results_file = results_file;
        args[i].completed_seeds = &completed_seeds;
        args[i].finished_threads = &finished_threads;
        next_offset += count;

        int error = pthread_create(&threads[i], NULL, worker_thread, &args[i]);
        if (error) {
            fprintf(stderr, "Unable to start thread %" PRIu64 ": %s\n",
                i, strerror(error));
            break;
        }
        started++;
    }
    if (started != thread_count) {
        for (size_t i = 0; i < started; i++)
            pthread_join(threads[i], NULL);
        fclose(results_file);
        free(threads);
        free(args);
        return EXIT_FAILURE;
    }

    while (atomic_load_explicit(&finished_threads, memory_order_relaxed) < thread_count) {
        struct timespec pause = {0, 250000000};
        nanosleep(&pause, NULL);
        clock_gettime(CLOCK_MONOTONIC, &now);
        uint64_t completed = atomic_load_explicit(&completed_seeds, memory_order_relaxed);
        double elapsed = elapsed_seconds(start, now);
        pthread_mutex_lock(&output_mutex);
        fprintf(stderr, "\nProgress: %6.2f%% (%" PRIu64 "/%" PRIu64
            ") | %.0f seeds/s | %.1fs elapsed",
            100.0 * completed / total_seeds, completed, total_seeds,
            elapsed > 0 ? completed / elapsed : 0, elapsed);
        last_reported = completed;
        progress_line_active = 1;
        fflush(stderr);
        pthread_mutex_unlock(&output_mutex);
    }
    for (size_t i = 0; i < started; i++)
        pthread_join(threads[i], NULL);
    clock_gettime(CLOCK_MONOTONIC, &now);
    double elapsed = elapsed_seconds(start, now);
    uint64_t completed = atomic_load_explicit(&completed_seeds, memory_order_relaxed);
    if (last_reported < total_seeds) {
        fprintf(stderr, "\nProgress: 100.00%% (%" PRIu64 "/%" PRIu64
            ") | %.0f seeds/s | %.1fs elapsed\n",
            completed, total_seeds, elapsed > 0 ? completed / elapsed : 0, elapsed);
    } else if (progress_line_active) {
        fputc('\n', stderr);
    }

    fclose(results_file);
    free(threads);
    free(args);
    return 0;
}