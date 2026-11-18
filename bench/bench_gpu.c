#include "c01/ctr.h"
#include "c01/gpu.h"

#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#ifdef _WIN32
#include <windows.h>
static double now(void)
{
    LARGE_INTEGER f, c;
    QueryPerformanceFrequency(&f);
    QueryPerformanceCounter(&c);
    return (double)c.QuadPart / (double)f.QuadPart;
}
#else
#include <time.h>
static double now(void)
{
    struct timespec t;
    clock_gettime(CLOCK_MONOTONIC, &t);
    return (double)t.tv_sec + (double)t.tv_nsec * 1e-9;
}
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
    size_t n = size_mib * MiB, i;
    unsigned char key[16], ctr[16] = {0, 1, 2, 3, 4, 5, 6, 7, 8, 9, 10, 11, 0, 0, 0, 0};
    unsigned char *in, *out;
    double t[MAX_RUNS], h[MAX_RUNS], k[MAX_RUNS], d[MAX_RUNS];
    c01_gpu *g;
    FILE *csv;
    int r;

    if (runs < 1 || runs > MAX_RUNS) {
        runs = 5;
    }
    for (i = 0; i < 16; ++i) {
        key[i] = (unsigned char)i;
    }
    in = malloc(n);
    out = malloc(n);
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
    printf("Dane: %zu MiB, GPU: %s\n", size_mib, c01_gpu_device_name(g));
    printf("OpenSSL (1 watek, CPU): %.2f GB/s\n", (double)n / median(t, runs) / 1e9);
    printf("%8s | %10s | %18s | %7s | %8s | %7s\n", "partia", "jadro GB/s", "z transferami GB/s", "H2D ms", "jadro ms", "D2H ms");

    csv = fopen(csv_path, "w");
    if (csv != NULL) {
        fprintf(csv, "mode,file_bytes,batch_bytes,run,t_h2d,t_kernel,t_d2h,t_proc,gbps_kernel,gbps_total,cache_state\n");
    }

    for (i = 0; i < sizeof(batches_mib) / sizeof(batches_mib[0]); ++i) {
        size_t b = batches_mib[i] * MiB;
        if (batches_mib[i] > size_mib) {
            break;
        }
        for (r = 0; r <= runs; ++r) {
            c01_gpu_times tm;
            if (c01_gpu_aes128_ctr(g, key, ctr, 0, in, out, n, b, &tm) != C01_CTR_OK) {
                fprintf(stderr, "GPU error\n");
                return 5;
            }
            if (r) {
                double total = tm.h2d + tm.kernel + tm.d2h;
                h[r - 1] = tm.h2d;
                k[r - 1] = tm.kernel;
                d[r - 1] = tm.d2h;
                t[r - 1] = total;
                if (csv != NULL) {
                    fprintf(csv, "gpu-opencl-sync,%zu,%zu,%d,%.6f,%.6f,%.6f,%.6f,%.3f,%.3f,mem\n",
                            n, b, r, tm.h2d, tm.kernel, tm.d2h, total,
                            (double)n / tm.kernel / 1e9, (double)n / total / 1e9);
                }
            }
        }
        {
            double mk = median(k, runs), mt = median(t, runs);
            printf("%4zu MiB | %10.2f | %18.2f | %7.1f | %8.1f | %7.1f\n", batches_mib[i],
                   (double)n / mk / 1e9, (double)n / mt / 1e9,
                   median(h, runs) * 1e3, mk * 1e3, median(d, runs) * 1e3);
        }
    }

    if (csv != NULL) {
        fclose(csv);
        printf("CSV: %s\n", csv_path);
    }
    c01_gpu_destroy(g);
    free(in);
    free(out);
    return 0;
}
