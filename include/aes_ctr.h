#ifndef AES_CTR_H
#define AES_CTR_H

#include <stddef.h>
#include <stdint.h>

#include "c01/ctr.h"

/* 128-bit initial counter reprezentowany jako 16 bajtów w porządku big-endian. */
typedef uint8_t ctr_counter[ C01_AES_BLOCK_SIZE ];

/* AES-128-CTR; szyfrowanie i deszyfrowanie to ta sama operacja. */
c01_ctr_status ctr_process(const uint8_t *in, uint8_t *out, size_t len,
                           const ctr_counter ctr0,
                           const uint8_t key[C01_AES128_KEY_SIZE]);

/* Implementacja CPU OpenMP tej samej operacji. */
c01_ctr_status ctr_process_omp(const uint8_t *in, uint8_t *out, size_t len,
                               const ctr_counter ctr0,
                               const uint8_t key[C01_AES128_KEY_SIZE]);

#endif
