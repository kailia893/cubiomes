#include "lb_noise.h"
//Compile: gcc lb_noise_demo.c lb_noise.c libcubiomes.a -pthread -lm -o test
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

int check_candidate_seed(uint64_t seed, int64_t x, int64_t z) {
    LbNoise n = {0};
    lb_setseed(&n, seed);
    //Early filters
    for (int i = 0; i < 4; i++) {
        if (lb_octave_prefix_sum(&n, NP_EROSION, 4, 
            x + positions[i][0], z + positions[i][1]) > -4000) return 0;
    }
    for (int i = 0; i < 4; i++) {
        if (lb_octave_prefix_sum(&n, NP_HUMIDITY, 2, 
            x + positions[i][0], z + positions[i][1]) < 1000) return 1;
    }
    for (int i = 0; i < 4; i++) {
        if (lb_octave_prefix_sum(&n, NP_TEMPERATURE, 4, 
            x + positions[i][0], z + positions[i][1]) < 5500) return 2;
    }
    for (int i = 0; i < 4; i++) {
        if (lb_octave_prefix_sum(&n, NP_CONTINENTALNESS, 2, 
            x + positions[i][0], z + positions[i][1]) < 0) return 3;
    }
    if (lb_octave_prefix_sum(&n, NP_TEMPERATURE, 4, x, z) < 5500) return 4;
    for (int i = 0; i < 6; i++) {
        for (int j = 0; j < 4; j++) {
            if (lb_octave_prefix_sum(&n, NP_TEMPERATURE, 4, 
                x + templimit[i][0] * multipliers[j][0], 
                z + templimit[i][1] * multipliers[j][1]) > 5500) return 5;
        }
    }
    //Floodfill
    int queue[20000][2] = {{x, z}};
    int head = 0;
    int tail = 1;
    int visited = 0;
    while (head < tail) {
        int cx = queue[head][0];
        int cz = queue[head][1];
        head++;
        int temperature = lb_octave_prefix_sum(&n, NP_TEMPERATURE, 4, cx, cz);
        if (temperature < 5500) {
            continue;
        }
        int humidity = lb_octave_prefix_sum(&n, NP_HUMIDITY, 2, cx, cz);
        if (humidity < 1000) {
            printf("Humidity too low at (%d, %d): %d\n", cx, cz, humidity);
            return 6;
        }
        int erosion = lb_octave_prefix_sum(&n, NP_EROSION, 4, cx, cz);
        if (erosion > -4000) {
            printf("Erosion too high at (%d, %d): %d\n", cx, cz, erosion);
            return 6;
        }
        int continental = lb_octave_prefix_sum(&n, NP_CONTINENTALNESS, 2, cx, cz);
        if (continental < 0) {
            printf("Continentalness too low at (%d, %d): %d\n", cx, cz, continental);
            return 6;
        }
        visited++;
        int dx[4] = {256, -256, 0, 0};
        int dz[4] = {0, 0, 256, -256};
        for (int dir = 0; dir < 4; dir++) {
            int nx = cx + dx[dir];
            int nz = cz + dz[dir];
            if (nx < -30000000 || nx > 30000000 || nz < -30000000 || nz > 30000000) {
                continue;
            }
            int duplicate = 0;
            for (int i = 0; i < tail; i++) {
                if (queue[i][0] == nx && queue[i][1] == nz) {
                    duplicate = 1;
                    break;
                }
            }
            if (duplicate) {
                continue;
            }
            if (tail >= 20000) {
                printf("Queue overflow, visited: %d\n", visited);
                return 65535 * visited;
            }
            queue[tail][0] = nx;
            queue[tail][1] = nz;
            tail++;
        }
    }
    return 65535 * visited;
}
typedef struct {
    int id;
    int64_t start_seed;
    int64_t end_seed;
    int64_t best[4];
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

static void *worker_thread(void *data) {
    WorkerArgs *args = data;
    int64_t *best = args->best;
    for (int64_t i = args->start_seed; i < args->end_seed; i++) {
        LbNoise n = {0};
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
            for (int k = 0; k < 5; k++) {
                if (lb_octave_int(&n, NP_EROSION, 0, 'A', 
                    x + positions[k][0], z + positions[k][1]
                    ) > 0) notbad = 0;
                if (!notbad) break;
            }
            if(!notbad) continue;
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
                for (int shiftx = (-4194304)*7; shiftx <= (4194304)*7; shiftx += 4194304) {
                    if (x2+shiftx < -30000000 || x2+shiftx > 30000000) continue;
                    for (int shiftz = (-4194304)*7; shiftz <= (4194304)*7; shiftz += 4194304) {
                        if (z2+shiftz < -30000000 || z2+shiftz > 30000000) continue;
                        int score = check_candidate_seed(to_unsigned(i), x2+shiftx, z2+shiftz);
                        if (score > best[0]) {
                            best[0] = score;
                            best[1] = i;
                            best[2] = x2+shiftx;
                            best[3] = z2+shiftz;
                            pthread_mutex_lock(&output_mutex);
                            if (progress_line_active)
                                fputc('\n', stderr);
                            fprintf(stderr, "Thread %d new best score: %d\n",
                                args->id, score);
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
    uint64_t total_seeds = 10000;
    uint64_t requested_threads = 4;
    uint64_t thread_count;
    uint64_t next_seed = 0;
    atomic_uint_fast64_t completed_seeds = 0;
    atomic_uint finished_threads = 0;
    pthread_t *threads;
    WorkerArgs *args;
    size_t started = 0;
    int64_t best[4] = {-1, -1, -1, -1};
    struct timespec start, now;

    if (argc > 3 || (argc > 1 && !parse_positive_u64(argv[1], &requested_threads)) ||
        (argc > 2 && !parse_positive_u64(argv[2], &total_seeds))) {
        fprintf(stderr, "Usage: %s [threads] [seed-count]\n", argv[0]);
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

    clock_gettime(CLOCK_MONOTONIC, &start);
    for (uint64_t i = 0; i < thread_count; i++) {
        uint64_t count = total_seeds / thread_count + (i < total_seeds % thread_count);
        args[i].id = (int)i;
        args[i].start_seed = (int64_t)next_seed;
        args[i].end_seed = (int64_t)(next_seed + count);
        args[i].completed_seeds = &completed_seeds;
        args[i].finished_threads = &finished_threads;
        args[i].best[0] = -1;
        args[i].best[1] = -1;
        args[i].best[2] = -1;
        args[i].best[3] = -1;
        next_seed += count;

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
        progress_line_active = 1;
        fflush(stderr);
        pthread_mutex_unlock(&output_mutex);
    }
    for (size_t i = 0; i < started; i++)
        pthread_join(threads[i], NULL);
    clock_gettime(CLOCK_MONOTONIC, &now);
    double elapsed = elapsed_seconds(start, now);
    uint64_t completed = atomic_load_explicit(&completed_seeds, memory_order_relaxed);
    fprintf(stderr, "\nProgress: 100.00%% (%" PRIu64 "/%" PRIu64
        ") | %.0f seeds/s | %.1fs elapsed\n",
        completed, total_seeds, elapsed > 0 ? completed / elapsed : 0, elapsed);

    for (size_t i = 0; i < started; i++) {
        if (args[i].best[0] > best[0]) {
            for (int j = 0; j < 4; j++)
                best[j] = args[i].best[j];
        }
    }
    printf("Best score: %" PRId64 "\n", best[0]);
    printf("Best seed: %" PRId64 "\n", best[1]);
    printf("Best position: (%" PRId64 ", %" PRId64 ")\n", best[2], best[3]);
    printf("Elapsed: %.1f seconds; average speed: %.0f seeds/s\n",
        elapsed, elapsed > 0 ? completed / elapsed : 0);
    free(threads);
    free(args);
    return 0;
}