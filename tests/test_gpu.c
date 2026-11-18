#include "c01/ctr.h"
#include "c01/gpu.h"

#include <stdio.h>
#include <stdlib.h>
#include <string.h>

static uint64_t rng_state = 0x9e3779b97f4a7c15ull;

static uint64_t rng(void)
{
    rng_state ^= rng_state << 13;
    rng_state ^= rng_state >> 7;
    rng_state ^= rng_state << 17;
    return rng_state;
}

static void fill(unsigned char *p, size_t n)
{
    size_t i;
    for (i = 0; i < n; ++i) {
        p[i] = (unsigned char)rng();
    }
}

static void hex(const char *h, unsigned char *out)
{
    size_t i;
    for (i = 0; h[2 * i]; ++i) {
        unsigned int v;
        sscanf(h + 2 * i, "%2x", &v);
        out[i] = (unsigned char)v;
    }
}

static void counter_add(unsigned char c[16], uint64_t n)
{
    int i;
    for (i = 15; i >= 0 && n != 0; --i) {
        uint64_t s = (uint64_t)c[i] + (n & 0xff);
        c[i] = (unsigned char)s;
        n = (n >> 8) + (s >> 8);
    }
}

/* GPU vs wrapper OpenSSL (c01_aes128_ctr_transform) */
static int same_as_openssl(c01_gpu *g, const unsigned char *key, const unsigned char *ctr,
                           uint64_t block_offset, size_t n, size_t batch)
{
    unsigned char *in = malloc(n + 1), *a = malloc(n + 1), *b = malloc(n + 1);
    unsigned char c[16];
    int ok;

    fill(in, n);
    memcpy(c, ctr, 16);
    counter_add(c, block_offset);
    ok = c01_aes128_ctr_transform(key, c, in, a, n) == C01_CTR_OK &&
         c01_gpu_aes128_ctr(g, key, ctr, block_offset, in, b, n, batch, NULL) == C01_CTR_OK &&
         memcmp(a, b, n) == 0;
    free(in);
    free(a);
    free(b);
    return ok;
}

static int report(const char *name, int ok)
{
    printf("%s %s\n", ok ? "OK  " : "BLAD", name);
    return ok;
}

int main(void)
{
    unsigned char key[16], ctr[16], pt[64], ct[64], out[64];
    c01_gpu *g = c01_gpu_create();
    int all = 1, ok, i;

    if (g == NULL) {
        fprintf(stderr, "no OpenCL GPU\n");
        return 5;
    }
    printf("GPU: %s\n", c01_gpu_device_name(g));

    hex("2b7e151628aed2a6abf7158809cf4f3c", key);
    hex("f0f1f2f3f4f5f6f7f8f9fafbfcfdfeff", ctr);
    hex("6bc1bee22e409f96e93d7e117393172aae2d8a571e03ac9c9eb76fac45af8e51"
        "30c81c46a35ce411e5fbc1191a0a52eff69f2445df4f9b17ad2b417be66c3710", pt);
    hex("874d6191b620e3261bef6864990db6ce9806f66b7970fdff8617187bb9fffdff"
        "5ae4df3edbd5d35e5b4f09020db03eab1e031dda2fbe03d1792170a0f3009cee", ct);
    ok = c01_gpu_aes128_ctr(g, key, ctr, 0, pt, out, 64, 0, NULL) == C01_CTR_OK && memcmp(out, ct, 64) == 0;
    ok &= c01_gpu_aes128_ctr(g, key, ctr, 1, pt + 16, out, 48, 0, NULL) == C01_CTR_OK && memcmp(out, ct + 16, 48) == 0;
    all &= report("T1 wektory NIST", ok);

    ok = 1;
    for (i = 0; i < 1000 && ok; ++i) {
        fill(key, 16);
        fill(ctr, 16);
        ok = same_as_openssl(g, key, ctr, rng() % 1000, (size_t)(rng() % 5000), 0);
    }
    all &= report("T2 zgodnosc z OpenSSL (1000 losowych)", ok);

    {
        size_t n = (1u << 20) + 5;
        unsigned char *in = malloc(n), *enc = malloc(n), *dec = malloc(n);
        fill(in, n);
        ok = c01_gpu_aes128_ctr(g, key, ctr, 0, in, enc, n, 65536, NULL) == C01_CTR_OK &&
             c01_gpu_aes128_ctr(g, key, ctr, 0, enc, dec, n, 65536, NULL) == C01_CTR_OK &&
             memcmp(in, dec, n) == 0;
        free(in);
        free(enc);
        free(dec);
        all &= report("T3 odzyskanie danych", ok);
    }

    {
        static const size_t sizes[] = {0, 1, 15, 16, 17, 31, 32, 33, 4097, 3 * 4096 + 1};
        ok = 1;
        for (i = 0; i < (int)(sizeof(sizes) / sizeof(sizes[0])); ++i) {
            ok &= same_as_openssl(g, key, ctr, 0, sizes[i], 4096);
        }
        all &= report("T4 przypadki brzegowe", ok);
    }

    {
        static const char *lows[] = {"ffffffffffffffff", "00000000ffffffff", "00000000fffffffe"};
        ok = 1;
        for (i = 0; i < 3; ++i) {
            char h[33];
            snprintf(h, sizeof(h), "0123456789abcdef%s", lows[i]);
            hex(h, ctr);
            ok &= same_as_openssl(g, key, ctr, 0, 64 * 16 + 7, 0);
        }
        all &= report("T5 przeniesienie licznika", ok);
    }

    {
        unsigned char buf[48] = {0};
        memset(ctr, 0xff, 16);
        ctr[15] = 0xfe;
        ok = same_as_openssl(g, key, ctr, 0, 32, 0) &&
             c01_gpu_aes128_ctr(g, key, ctr, 0, buf, buf, 33, 0, NULL) == C01_CTR_COUNTER_OVERFLOW;
        all &= report("T6 granica zakresu (R1)", ok);
    }

    {
        static const size_t batches[] = {16, 4096, 65536, 1u << 20, 4u << 20};
        ok = 1;
        fill(ctr, 16);
        for (i = 0; i < 5; ++i) {
            rng_state = 42;
            ok &= same_as_openssl(g, key, ctr, 0, (3u << 20) + 9, batches[i]);
        }
        all &= report("T7 niezaleznosc od podzialu", ok);
    }

    c01_gpu_destroy(g);
    return all ? 0 : 1;
}
