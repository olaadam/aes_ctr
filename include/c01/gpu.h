#ifndef C01_GPU_H
#define C01_GPU_H

#include <stddef.h>
#include <stdint.h>

#include "c01/ctr.h"

typedef struct c01_gpu c01_gpu;

typedef struct {
    double h2d;
    double kernel;
    double d2h;
} c01_gpu_times;

/* Tworzy kontekst OpenCL na pierwszym GPU i kompiluje jądro. NULL przy braku urządzenia. */
c01_gpu *c01_gpu_create(void);
void c01_gpu_destroy(c01_gpu *gpu);
const char *c01_gpu_device_name(const c01_gpu *gpu);

/*
AES-128-CTR na GPU, synchronicznie: dla każdej partii H2D, jądro, D2H.
Licznik pierwszego bloku = initial_counter + block_offset (128 bit, big-endian).
batch: rozmiar partii w bajtach, wielokrotność 16; 0 = cały bufor naraz.
times: sumy czasów etapów w sekundach (może być NULL).
*/
c01_ctr_status c01_gpu_aes128_ctr(
    c01_gpu *gpu,
    const unsigned char key[C01_AES128_KEY_SIZE],
    const unsigned char initial_counter[C01_AES_BLOCK_SIZE],
    uint64_t block_offset,
    const unsigned char *input,
    unsigned char *output,
    size_t length,
    size_t batch,
    c01_gpu_times *times);

#endif
