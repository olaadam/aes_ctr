#ifndef C01_CTR_H
#define C01_CTR_H

#include <stddef.h>

#define C01_AES128_KEY_SIZE 16
#define C01_AES_BLOCK_SIZE 16

typedef enum {
    C01_CTR_OK = 0,
    C01_CTR_INVALID_ARGUMENT,
    C01_CTR_COUNTER_OVERFLOW,
    C01_CTR_CRYPTO_ERROR
} c01_ctr_status;

/*
Szyfruje lub deszyfruje length bajtów za pomocą AES-128-CTR.
Licznik początkowy ma 16 bajtów i jest zapisany w kolejności big-endian.
Obsługiwana jest operacja w miejscu (input == output). Niepuste dane
wejściowe i wyjściowe nie mogą na siebie nachodzić.
*/
c01_ctr_status c01_aes128_ctr_transform(
    const unsigned char key[C01_AES128_KEY_SIZE],
    const unsigned char initial_counter[C01_AES_BLOCK_SIZE],
    const unsigned char *input,
    unsigned char *output,
    size_t length);

const char *c01_ctr_status_string(c01_ctr_status status);

#endif
