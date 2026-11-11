#include "c01/ctr.h"

#include <stdio.h>
#include <string.h>

static int from_hex(const char *hex, unsigned char *output, size_t output_size)
{
    size_t index;

    if (strlen(hex) != output_size * 2) {
        return 0;
    }

    for (index = 0; index < output_size; ++index) {
        unsigned int value;
        if (sscanf(hex + index * 2, "%2x", &value) != 1) {
            return 0;
        }
        output[index] = (unsigned char)value;
    }

    return 1;
}

static int test_nist_vector(void)
{
    unsigned char key[C01_AES128_KEY_SIZE];
    unsigned char counter[C01_AES_BLOCK_SIZE];
    unsigned char plaintext[64];
    unsigned char expected[64];
    unsigned char ciphertext[64];
    unsigned char recovered[64];

    /*Wartości testowe z dokumentacji NIST 
    klucz: 2b7e151628aed2a6abf7158809cf4f3c
    licznik początkowy: f0f1f2f3f4f5f6f7f8f9fafbfcfdfeff
    dane wejściowe: 6bc1bee22e409f96e93d7e117393172aae2d8a571e03ac9c9eb76fac45af8e5130c81c46a35ce411e5fbc1191a0a52eff69f2445df4f9b17ad2b417be66c3710
    dane wyjściowe: 874d6191b620e3261bef6864990db6ce9806f66b7970fdff8617187bb9fffdff5ae4df3edbd5d35e5b4f09020db03eab1e031dda2fbe03d1792170a0f3009cee
    */

    if (!from_hex("2b7e151628aed2a6abf7158809cf4f3c", key, sizeof(key)) ||
        !from_hex("f0f1f2f3f4f5f6f7f8f9fafbfcfdfeff", counter, sizeof(counter)) ||
        !from_hex(
            "6bc1bee22e409f96e93d7e117393172a"
            "ae2d8a571e03ac9c9eb76fac45af8e51"
            "30c81c46a35ce411e5fbc1191a0a52ef"
            "f69f2445df4f9b17ad2b417be66c3710",
            plaintext, sizeof(plaintext)) ||
        !from_hex(
            "874d6191b620e3261bef6864990db6ce"
            "9806f66b7970fdff8617187bb9fffdff"
            "5ae4df3edbd5d35e5b4f09020db03eab"
            "1e031dda2fbe03d1792170a0f3009cee",
            expected, sizeof(expected))) {
        return 0;
    }

    if (c01_aes128_ctr_transform(
            key, counter, plaintext, ciphertext, sizeof(plaintext)) != C01_CTR_OK ||
        memcmp(ciphertext, expected, sizeof(expected)) != 0) {
        return 0;
    }

    return c01_aes128_ctr_transform(
               key, counter, ciphertext, recovered, sizeof(ciphertext)) == C01_CTR_OK &&
           memcmp(recovered, plaintext, sizeof(plaintext)) == 0;
}

static int test_partial_block_and_counter_overflow(void)
{
    unsigned char key[C01_AES128_KEY_SIZE] = {0};
    unsigned char counter[C01_AES_BLOCK_SIZE];
    unsigned char input[17] = {0};
    unsigned char output[sizeof(input)];
    size_t index;

    memset(counter, 0xff, sizeof(counter));
    if (c01_aes128_ctr_transform(key, counter, input, output, 1) != C01_CTR_OK) {
        return 0;
    }
    if (c01_aes128_ctr_transform(
            key, counter, input, output, sizeof(input)) != C01_CTR_COUNTER_OVERFLOW) {
        return 0;
    }

    for (index = 0; index < sizeof(counter); ++index) {
        counter[index] = 0;
    }
    return c01_aes128_ctr_transform(key, counter, NULL, NULL, 0) == C01_CTR_OK;
}

static int test_openmp_matches_serial(void)
{
    unsigned char key[16], counter[16], input[4099], serial[4099], parallel[4099];
    size_t i;
    if (!from_hex("2b7e151628aed2a6abf7158809cf4f3c", key, sizeof(key)) ||
        !from_hex("f0f1f2f3f4f5f6f7f8f9fafbfcfdfeff", counter, sizeof(counter)))
        return 0;
    for (i = 0; i < sizeof(input); ++i) input[i] = (unsigned char)(i * 37u + 11u);
    if (c01_aes128_ctr_transform(key, counter, input, serial, sizeof(input)) != C01_CTR_OK ||
        c01_aes128_ctr_transform_omp(key, counter, input, parallel, sizeof(input)) != C01_CTR_OK ||
        memcmp(serial, parallel, sizeof(serial)) != 0)
        return 0;
    return c01_aes128_ctr_transform_omp(key, counter, parallel, parallel, sizeof(parallel)) == C01_CTR_OK &&
           c01_aes128_ctr_transform(key, counter, parallel, serial, sizeof(serial)) == C01_CTR_OK &&
           memcmp(serial, input, sizeof(input)) == 0;
}

int main(void)
{
    if (!test_nist_vector()) {
        fprintf(stderr, "NIST AES-128-CTR vector or round-trip test failed\n");
        return 1;
    }
    if (!test_partial_block_and_counter_overflow()) {
        fprintf(stderr, "partial-block, empty-input, or counter-range test failed\n");
        return 1;
    }
    if (!test_openmp_matches_serial()) {
        fprintf(stderr, "OpenMP AES-128-CTR comparison or round-trip test failed\n");
        return 1;
    }

    puts("All AES-128-CTR tests passed");
    return 0;
}
