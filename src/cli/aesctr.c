#include "aes_ctr.h"

#include <errno.h>
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

typedef enum { MODE_SERIAL, MODE_OMP } cpu_mode;

static int parse_hex(const char *s, uint8_t *out, size_t n)
{
    size_t i;
    if (strlen(s) != n * 2) return 0;
    for (i = 0; i < n; ++i) {
        unsigned int hi, lo;
        char a = s[2 * i], b = s[2 * i + 1];
        if (a >= '0' && a <= '9') hi = (unsigned int)(a - '0');
        else if (a >= 'a' && a <= 'f') hi = (unsigned int)(a - 'a' + 10);
        else if (a >= 'A' && a <= 'F') hi = (unsigned int)(a - 'A' + 10);
        else return 0;
        if (b >= '0' && b <= '9') lo = (unsigned int)(b - '0');
        else if (b >= 'a' && b <= 'f') lo = (unsigned int)(b - 'a' + 10);
        else if (b >= 'A' && b <= 'F') lo = (unsigned int)(b - 'A' + 10);
        else return 0;
        out[i] = (uint8_t)((hi << 4) | lo);
    }
    return 1;
}

static int parse_size(const char *s, size_t *out)
{
    char *end;
    unsigned long long value;
    errno = 0;
    value = strtoull(s, &end, 10);
    if (errno || end == s || *end || value == 0 || value > SIZE_MAX) return 0;
    *out = (size_t)value;
    return 1;
}

static int add_blocks(const uint8_t start[16], size_t blocks, uint8_t result[16])
{
    size_t carry = blocks;
    int i;
    memcpy(result, start, 16);
    for (i = 15; i >= 0 && carry; --i) {
        size_t sum = (size_t)result[i] + (carry & 0xffu);
        result[i] = (uint8_t)sum;
        carry = (carry >> 8) + (sum >> 8);
    }
    return carry == 0;
}

static void usage(FILE *f)
{
    fprintf(f, "Usage: aesctr <in> <out> --key <32-hex> --ctr <32-hex> "
               "--buf <bytes> --mode {serial|omp|mpi|gpu}\n");
}

int main(int argc, char **argv)
{
    const char *input_path, *output_path, *key_hex = NULL, *ctr_hex = NULL;
    const char *mode = NULL;
    uint8_t key[16], ctr0[16], ctr[16];
    size_t buffer_size = 0, chunk_capacity;
    cpu_mode selected;
    FILE *input = NULL, *output = NULL;
    uint8_t *inbuf = NULL, *outbuf = NULL;
    size_t n;
    int counter_exhausted = 0;
    int result = 1;
    int i;

    if (argc < 3) { usage(stderr); return 2; }
    input_path = argv[1]; output_path = argv[2];
    for (i = 3; i < argc; ++i) {
        if (i + 1 >= argc) { usage(stderr); return 2; }
        if (!strcmp(argv[i], "--key")) key_hex = argv[++i];
        else if (!strcmp(argv[i], "--ctr")) ctr_hex = argv[++i];
        else if (!strcmp(argv[i], "--buf")) {
            if (!parse_size(argv[++i], &buffer_size)) {
                fprintf(stderr, "Invalid --buf (expected positive byte count)\n"); return 2;
            }
        } else if (!strcmp(argv[i], "--mode")) mode = argv[++i];
        else { fprintf(stderr, "Unknown option: %s\n", argv[i]); usage(stderr); return 2; }
    }
    if (!key_hex || !ctr_hex || !mode || buffer_size < 16 ||
        !parse_hex(key_hex, key, sizeof(key)) || !parse_hex(ctr_hex, ctr0, sizeof(ctr0))) {
        fprintf(stderr, "Invalid or missing key, counter, buffer size, or mode\n"); usage(stderr); return 2;
    }
    if (!strcmp(mode, "serial")) selected = MODE_SERIAL;
    else if (!strcmp(mode, "omp")) selected = MODE_OMP;
    else if (!strcmp(mode, "mpi") || !strcmp(mode, "gpu")) {
        fprintf(stderr, "Mode '%s' is not available in this CPU executable\n", mode); return 2;
    } else { fprintf(stderr, "Unknown mode: %s\n", mode); usage(stderr); return 2; }

    input = fopen(input_path, "rb");
    if (!input) { perror(input_path); goto cleanup; }
    output = fopen(output_path, "wb");
    if (!output) { perror(output_path); goto cleanup; }
    inbuf = (uint8_t *)malloc(buffer_size);
    outbuf = (uint8_t *)malloc(buffer_size);
    if (!inbuf || !outbuf) { fprintf(stderr, "Cannot allocate requested buffer\n"); goto cleanup; }
    chunk_capacity = buffer_size - buffer_size % 16;

    memcpy(ctr, ctr0, sizeof(ctr));
    while ((n = fread(inbuf, 1, chunk_capacity, input)) != 0) {
        c01_ctr_status status;
        if (counter_exhausted) {
            fprintf(stderr, "CTR counter range exhausted\n"); goto cleanup;
        }
        if (selected == MODE_OMP)
            status = c01_aes128_ctr_transform_omp(key, ctr, inbuf, outbuf, n);
        else
            status = c01_aes128_ctr_transform(key, ctr, inbuf, outbuf, n);
        if (status != C01_CTR_OK) {
            fprintf(stderr, "CTR processing failed: %s\n", c01_ctr_status_string(status)); goto cleanup;
        }
        if (fwrite(outbuf, 1, n, output) != n) { perror("write"); goto cleanup; }
        /* Full chunks end on a block boundary; only the final chunk can be partial. */
        if (n == chunk_capacity) {
            size_t blocks = n / 16 + (n % 16 != 0);
            uint8_t next[16];
            counter_exhausted = !add_blocks(ctr, blocks, next);
            memcpy(ctr, next, sizeof(ctr));
        }
    }
    if (ferror(input)) { perror("read"); goto cleanup; }
    if (fflush(output) != 0) { perror("flush"); goto cleanup; }
    result = 0;

cleanup:
    free(inbuf); free(outbuf);
    if (input) fclose(input);
    if (output && fclose(output) != 0) { perror("close output"); result = 1; }
    return result;
}
