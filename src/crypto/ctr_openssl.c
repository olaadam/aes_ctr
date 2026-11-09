#include "c01/ctr.h"

#include <limits.h>
#include <stdint.h>

#include <openssl/evp.h>

static int counter_range_fits(
    const unsigned char initial_counter[C01_AES_BLOCK_SIZE],
    size_t block_count)
{
    unsigned char last_counter[C01_AES_BLOCK_SIZE];
    size_t addend;
    int index;

    if (block_count == 0) {
        return 1;
    }

    for (index = 0; index < C01_AES_BLOCK_SIZE; ++index) {
        last_counter[index] = initial_counter[index];
    }

    addend = block_count - 1;
    for (index = C01_AES_BLOCK_SIZE - 1; index >= 0 && addend != 0; --index) {
        size_t sum = (size_t)last_counter[index] + (addend & 0xffu);
        last_counter[index] = (unsigned char)(sum & 0xffu);
        addend = (addend >> 8) + (sum >> 8);
    }

    return addend == 0;
}

c01_ctr_status c01_aes128_ctr_transform(
    const unsigned char key[C01_AES128_KEY_SIZE],
    const unsigned char initial_counter[C01_AES_BLOCK_SIZE],
    const unsigned char *input,
    unsigned char *output,
    size_t length)
{
    EVP_CIPHER_CTX *context;
    unsigned char final_buffer[EVP_MAX_BLOCK_LENGTH];
    size_t block_count;
    size_t offset = 0;
    int final_length = 0;
    c01_ctr_status status = C01_CTR_CRYPTO_ERROR;

    if (key == NULL || initial_counter == NULL ||
        (length != 0 && (input == NULL || output == NULL))) {
        return C01_CTR_INVALID_ARGUMENT;
    }

    block_count = length / C01_AES_BLOCK_SIZE;
    if (length % C01_AES_BLOCK_SIZE != 0) {
        ++block_count;
    }
    if (!counter_range_fits(initial_counter, block_count)) {
        return C01_CTR_COUNTER_OVERFLOW;
    }

    context = EVP_CIPHER_CTX_new();
    if (context == NULL) {
        return C01_CTR_CRYPTO_ERROR;
    }

    if (EVP_EncryptInit_ex(
            context, EVP_aes_128_ctr(), NULL, key, initial_counter) != 1) {
        goto cleanup;
    }

    while (offset < length) {
        size_t remaining = length - offset;
        int chunk_length = remaining > (size_t)INT_MAX
                               ? INT_MAX
                               : (int)remaining;
        int written = 0;

        if (EVP_EncryptUpdate(
                context, output + offset, &written, input + offset,
                chunk_length) != 1 ||
            written != chunk_length) {
            goto cleanup;
        }
        offset += (size_t)chunk_length;
    }

    if (EVP_EncryptFinal_ex(context, final_buffer, &final_length) != 1 ||
        final_length != 0) {
        goto cleanup;
    }

    status = C01_CTR_OK;

cleanup:
    EVP_CIPHER_CTX_free(context);
    return status;
}

const char *c01_ctr_status_string(c01_ctr_status status)
{
    switch (status) {
    case C01_CTR_OK:
        return "success";
    case C01_CTR_INVALID_ARGUMENT:
        return "invalid argument";
    case C01_CTR_COUNTER_OVERFLOW:
        return "counter range overflows 128 bits";
    case C01_CTR_CRYPTO_ERROR:
        return "OpenSSL EVP operation failed";
    default:
        return "unknown CTR status";
    }
}
