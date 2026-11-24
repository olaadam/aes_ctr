#include "c01/ctr.h"

#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <omp.h>

#ifdef _WIN32
#include <windows.h>
static double now_seconds(void)
{
    LARGE_INTEGER frequency, value;
    QueryPerformanceFrequency(&frequency);
    QueryPerformanceCounter(&value);
    return (double)value.QuadPart / (double)frequency.QuadPart;
}
#else
#include <time.h>
static double now_seconds(void)
{
    struct timespec value;
    clock_gettime(CLOCK_MONOTONIC, &value);
    return (double)value.tv_sec + (double)value.tv_nsec * 1e-9;
}
#endif

static int compare_double(const void *a, const void *b)
{
    double x = *(const double *)a, y = *(const double *)b;
    return (x > y) - (x < y);
}

static double median(double *values, size_t count)
{
    qsort(values, count, sizeof(*values), compare_double);
    return count % 2 ? values[count / 2]
                     : (values[count / 2 - 1] + values[count / 2]) / 2.0;
}

int main(int argc, char **argv)
{
    size_t bytes = argc > 1 ? (size_t)strtoull(argv[1], NULL, 10) : 64u * 1024u * 1024u;
    size_t runs = argc > 2 ? (size_t)strtoull(argv[2], NULL, 10) : 7;
    const char *csv_path = argc > 3 ? argv[3] : NULL;
    unsigned char key[16] = {0}, counter[16] = {0};
    unsigned char *input, *output;
    double *samples;
    FILE *csv = NULL;
    size_t i;
    int mode;

    if (bytes == 0 || runs == 0 || runs > 1000) {
        fprintf(stderr, "Usage: bench_cpu [bytes>0] [runs 1..1000] [optional.csv]\n");
        return 2;
    }
    input = (unsigned char *)malloc(bytes);
    output = (unsigned char *)malloc(bytes);
    samples = (double *)malloc(runs * sizeof(*samples));
    if (!input || !output || !samples) {
        fprintf(stderr, "Cannot allocate benchmark buffers\n");
        free(input); free(output); free(samples);
        return 2;
    }
    for (i = 0; i < bytes; ++i) input[i] = (unsigned char)(i * 31u + 7u);
    for (i = 0; i < sizeof(key); ++i) key[i] = (unsigned char)i;
    counter[15] = 1;

    if (csv_path) {
        csv = fopen(csv_path, "w");
        if (!csv) { perror(csv_path); free(input); free(output); free(samples); return 2; }
        fputs("mode,threads,bytes,runs,median_seconds,gb_per_second\n", csv);
    }
    printf("CPU kernel benchmark: %zu bytes, %zu runs (no file I/O)\n", bytes, runs);
    puts("mode   threads   median_ms   throughput_GB/s");
    for (mode = 0; mode < 2; ++mode) {
        c01_ctr_status (*transform)(const unsigned char *, const unsigned char *,
                                    const unsigned char *, unsigned char *, size_t) =
            mode == 0 ? c01_aes128_ctr_transform : c01_aes128_ctr_transform_omp;
        for (i = 0; i < runs + 1; ++i) {
            double start = now_seconds();
            c01_ctr_status status = transform(key, counter, input, output, bytes);
            double elapsed = now_seconds() - start;
            if (status != C01_CTR_OK) {
                fprintf(stderr, "CTR failed: %s\n", c01_ctr_status_string(status));
                if (csv) fclose(csv);
                free(input); free(output); free(samples);
                return 1;
            }
            if (i != 0) samples[i - 1] = elapsed; /* first call warms EVP/OpenMP */
        }
        {
            double med = median(samples, runs);
            double throughput = (double)bytes / med / 1e9;
            const char *name = mode == 0 ? "serial" : "openmp";
            int threads = mode == 0 ? 1 : omp_get_max_threads();
            printf("%-6s %7d %10.3f %18.3f\n", name, threads, med * 1e3, throughput);
            if (csv) fprintf(csv, "%s,%d,%zu,%zu,%.9f,%.6f\n", name, threads, bytes, runs, med, throughput);
        }
    }
    if (csv && fclose(csv) != 0) { perror("close CSV"); free(input); free(output); free(samples); return 1; }
    free(input); free(output); free(samples);
    return 0;
}
