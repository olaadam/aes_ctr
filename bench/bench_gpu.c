#include "c01/ctr.h"
#include "c01/gpu.h"

#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#ifdef _WIN32
#include <malloc.h>
#include <windows.h>
static double now(void)
{
    LARGE_INTEGER f, c;
    QueryPerformanceFrequency(&f);
    QueryPerformanceCounter(&c);
    return (double)c.QuadPart / (double)f.QuadPart;
}
static void *alloc_aligned(size_t n) { return _aligned_malloc(n, 4096); }
static void free_aligned(void *p) { _aligned_free(p); }
#else
#include <time.h>
static double now(void)
{
    struct timespec t;
    clock_gettime(CLOCK_MONOTONIC, &t);
    return (double)t.tv_sec + (double)t.tv_nsec * 1e-9;
}
static void *alloc_aligned(size_t n) { return aligned_alloc(4096, (n + 4095) / 4096 * 4096); }
static void free_aligned(void *p) { free(p); }
#endif

#define MiB ((size_t)1 << 20)
#define MAX_RUNS 32

static int cmp_double(const void *a, const void *b)
{
    double x = *(const double *)a, y = *(const double *)b;
    return (x > y) - (x < y);
}

static double median(double *v, int n)
{
    qsort(v, (size_t)n, sizeof(double), cmp_double);
    return n % 2 ? v[n / 2] : 0.5 * (v[n / 2 - 1] + v[n / 2]);
}

int main(int argc, char **argv)
{
    size_t size_mib = argc > 1 ? (size_t)atoi(argv[1]) : 256;
    int runs = argc > 2 ? atoi(argv[2]) : 5;
    const char *csv_path = argc > 3 ? argv[3] : "results/gpu_opencl_batch_c.csv";
    static const size_t batches_mib[] = {1, 4, 16, 64, 256};
    static const c01_gpu_mem mems[] = {C01_GPU_COPY, C01_GPU_ZEROCOPY};
    static const char *mem_names[] = {"copy", "zerocopy"};
    size_t n = size_mib * MiB, i;
    unsigned char key[16], ctr[16] = {0, 1, 2, 3, 4, 5, 6, 7, 8, 9, 10, 11, 0, 0, 0, 0};
    unsigned char *in, *out;
    double t[MAX_RUNS], h[MAX_RUNS], k[MAX_RUNS], d[MAX_RUNS], w[MAX_RUNS];
    c01_gpu *g;
    FILE *csv;
    int r, mi;

    if (runs < 1 || runs > MAX_RUNS) {
        runs = 5;
    }
    for (i = 0; i < 16; ++i) {
        key[i] = (unsigned char)i;
    }
    in = alloc_aligned(n);
    out = alloc_aligned(n);
    if (in == NULL || out == NULL) {
        fprintf(stderr, "out of memory\n");
        return 2;
    }
    for (i = 0; i < n; ++i) {
        in[i] = (unsigned char)(i * 2654435761u >> 13);
    }

    g = c01_gpu_create();
    if (g == NULL) {
        fprintf(stderr, "no OpenCL GPU\n");
        return 5;
    }

    for (r = 0; r <= runs; ++r) {
        double t0 = now();
        c01_aes128_ctr_transform(key, ctr, in, out, n);
        if (r) {
            t[r - 1] = now() - t0;
        }
    }
    printf("Dane: %zu MiB (bufory wyrownane do 4096 B), GPU: %s\n", size_mib, c01_gpu_device_name(g));
    printf("OpenSSL (1 watek, CPU): %.2f GB/s\n", (double)n / median(t, runs) / 1e9);
    printf("%-8s | %8s | %10s | %13s | %12s | %7s | %8s | %7s | %s\n", "tryb", "partia", "jadro GB/s",
           "z transf GB/s", "zegar GB/s", "H2D ms", "jadro ms", "D2H ms", "zero-copy");

    csv = fopen(csv_path, "w");
    if (csv != NULL) {
        fprintf(csv, "mode,mem,file_bytes,batch_bytes,run,t_h2d,t_kernel,t_d2h,t_proc,t_wall,"
                     "gbps_kernel,gbps_total,gbps_wall,zero_copy_batches,batches,cache_state\n");
    }

    for (mi = 0; mi < 2; ++mi) {
        for (i = 0; i < sizeof(batches_mib) / sizeof(batches_mib[0]); ++i) {
            size_t b = batches_mib[i] * MiB;
            c01_gpu_times tm;
            if (batches_mib[i] > size_mib) {
                break;
            }
            for (r = 0; r <= runs; ++r) {
                double t0 = now(), wall;
                if (c01_gpu_aes128_ctr_mem(g, key, ctr, 0, in, out, n, b, mems[mi], &tm) != C01_CTR_OK) {
                    fprintf(stderr, "GPU error\n");
                    return 5;
                }
                wall = now() - t0;
                if (r) {
                    double total = tm.h2d + tm.kernel + tm.d2h;
                    h[r - 1] = tm.h2d;
                    k[r - 1] = tm.kernel;
                    d[r - 1] = tm.d2h;
                    t[r - 1] = total;
                    w[r - 1] = wall;
                    if (csv != NULL) {
                        fprintf(csv, "gpu-opencl-sync,%s,%zu,%zu,%d,%.6f,%.6f,%.6f,%.6f,%.6f,%.3f,%.3f,%.3f,%d,%d,mem\n",
                                mem_names[mi], n, b, r, tm.h2d, tm.kernel, tm.d2h, total, wall,
                                (double)n / tm.kernel / 1e9, (double)n / total / 1e9, (double)n / wall / 1e9,
                                tm.zero_copy_batches, tm.batches);
                    }
                }
            }
            {
                double mk = median(k, runs), mt = median(t, runs), mw = median(w, runs);
                printf("%-8s | %4zu MiB | %10.2f | %13.2f | %12.2f | %7.1f | %8.1f | %7.1f | %d/%d\n",
                       mem_names[mi], batches_mib[i], (double)n / mk / 1e9, (double)n / mt / 1e9,
                       (double)n / mw / 1e9, median(h, runs) * 1e3, mk * 1e3, median(d, runs) * 1e3,
                       mems[mi] == C01_GPU_ZEROCOPY ? tm.zero_copy_batches : 0, tm.batches);
            }
        }
    }

    if (csv != NULL) {
        fclose(csv);
        printf("CSV: %s\n", csv_path);
    }
    c01_gpu_destroy(g);
    free_aligned(in);
    free_aligned(out);
    return 0;
}
