#define CL_TARGET_OPENCL_VERSION 300

#include "c01/gpu.h"

#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include <CL/cl.h>

#define LOCAL_SIZE 256

struct c01_gpu {
    cl_device_id device;
    cl_context context;
    cl_command_queue queue;
    cl_program program;
    cl_kernel kernel;
    cl_mem te0;
    cl_mem sbox;
    char name[256];
};

static unsigned char SBOX[256];
static uint32_t TE0[256];

static unsigned char xtime(unsigned char b)
{
    return (unsigned char)((b << 1) ^ ((b & 0x80) ? 0x1b : 0));
}

static unsigned char rotl8(unsigned char x, int s)
{
    return (unsigned char)((x << s) | (x >> (8 - s)));
}

static void init_tables(void)
{
    unsigned char p = 1, q = 1;
    int i;

    do {
        p = (unsigned char)(p ^ (p << 1) ^ ((p & 0x80) ? 0x1b : 0));
        q ^= (unsigned char)(q << 1);
        q ^= (unsigned char)(q << 2);
        q ^= (unsigned char)(q << 4);
        if (q & 0x80) {
            q ^= 0x09;
        }
        SBOX[p] = (unsigned char)(q ^ rotl8(q, 1) ^ rotl8(q, 2) ^ rotl8(q, 3) ^ rotl8(q, 4) ^ 0x63);
    } while (p != 1);
    SBOX[0] = 0x63;

    for (i = 0; i < 256; ++i) {
        unsigned char s = SBOX[i];
        TE0[i] = ((uint32_t)xtime(s) << 24) | ((uint32_t)s << 16) | ((uint32_t)s << 8) | (uint32_t)(xtime(s) ^ s);
    }
}

static void expand_key(const unsigned char key[16], cl_uint rk[44])
{
    unsigned char rcon = 1;
    int i;

    for (i = 0; i < 4; ++i) {
        rk[i] = ((cl_uint)key[4 * i] << 24) | ((cl_uint)key[4 * i + 1] << 16) | ((cl_uint)key[4 * i + 2] << 8) | key[4 * i + 3];
    }
    for (i = 4; i < 44; ++i) {
        cl_uint t = rk[i - 1];
        if (i % 4 == 0) {
            t = (t << 8) | (t >> 24);
            t = ((cl_uint)SBOX[t >> 24] << 24) | ((cl_uint)SBOX[(t >> 16) & 0xff] << 16) |
                ((cl_uint)SBOX[(t >> 8) & 0xff] << 8) | SBOX[t & 0xff];
            t ^= (cl_uint)rcon << 24;
            rcon = xtime(rcon);
        }
        rk[i] = rk[i - 4] ^ t;
    }
}

static char *read_file(const char *path, size_t *size)
{
    FILE *f = fopen(path, "rb");
    char *buf;
    long n;

    if (f == NULL) {
        return NULL;
    }
    fseek(f, 0, SEEK_END);
    n = ftell(f);
    fseek(f, 0, SEEK_SET);
    buf = malloc((size_t)n + 1);
    if (buf != NULL && fread(buf, 1, (size_t)n, f) == (size_t)n) {
        buf[n] = '\0';
        *size = (size_t)n;
    } else {
        free(buf);
        buf = NULL;
    }
    fclose(f);
    return buf;
}

c01_gpu *c01_gpu_create(void)
{
    cl_platform_id platforms[8];
    cl_uint nplat = 0, i;
    cl_int err;
    c01_gpu *g = calloc(1, sizeof(*g));
    cl_queue_properties props[] = {CL_QUEUE_PROPERTIES, CL_QUEUE_PROFILING_ENABLE, 0};
    char *src;
    size_t src_len;

    if (g == NULL) {
        return NULL;
    }
    init_tables();

    if (clGetPlatformIDs(8, platforms, &nplat) != CL_SUCCESS) {
        goto fail;
    }
    for (i = 0; i < nplat && g->device == NULL; ++i) {
        if (clGetDeviceIDs(platforms[i], CL_DEVICE_TYPE_GPU, 1, &g->device, NULL) != CL_SUCCESS) {
            g->device = NULL;
        }
    }
    if (g->device == NULL) {
        goto fail;
    }
    clGetDeviceInfo(g->device, CL_DEVICE_NAME, sizeof(g->name), g->name, NULL);

    g->context = clCreateContext(NULL, 1, &g->device, NULL, NULL, &err);
    if (err != CL_SUCCESS) {
        goto fail;
    }
    g->queue = clCreateCommandQueueWithProperties(g->context, g->device, props, &err);
    if (err != CL_SUCCESS) {
        goto fail;
    }

    src = read_file(C01_GPU_KERNEL_PATH, &src_len);
    if (src == NULL) {
        fprintf(stderr, "cannot read kernel: %s\n", C01_GPU_KERNEL_PATH);
        goto fail;
    }
    g->program = clCreateProgramWithSource(g->context, 1, (const char **)&src, &src_len, &err);
    free(src);
    if (err != CL_SUCCESS) {
        goto fail;
    }
    if (clBuildProgram(g->program, 1, &g->device, "", NULL, NULL) != CL_SUCCESS) {
        char log[8192];
        clGetProgramBuildInfo(g->program, g->device, CL_PROGRAM_BUILD_LOG, sizeof(log), log, NULL);
        fprintf(stderr, "kernel build failed:\n%s\n", log);
        goto fail;
    }
    g->kernel = clCreateKernel(g->program, "aes128_ctr", &err);
    if (err != CL_SUCCESS) {
        goto fail;
    }
    g->te0 = clCreateBuffer(g->context, CL_MEM_READ_ONLY | CL_MEM_COPY_HOST_PTR, sizeof(TE0), TE0, &err);
    if (err != CL_SUCCESS) {
        goto fail;
    }
    g->sbox = clCreateBuffer(g->context, CL_MEM_READ_ONLY | CL_MEM_COPY_HOST_PTR, sizeof(SBOX), SBOX, &err);
    if (err != CL_SUCCESS) {
        goto fail;
    }
    return g;

fail:
    c01_gpu_destroy(g);
    return NULL;
}

void c01_gpu_destroy(c01_gpu *g)
{
    if (g == NULL) {
        return;
    }
    if (g->sbox) clReleaseMemObject(g->sbox);
    if (g->te0) clReleaseMemObject(g->te0);
    if (g->kernel) clReleaseKernel(g->kernel);
    if (g->program) clReleaseProgram(g->program);
    if (g->queue) clReleaseCommandQueue(g->queue);
    if (g->context) clReleaseContext(g->context);
    free(g);
}

const char *c01_gpu_device_name(const c01_gpu *g)
{
    return g->name;
}

static double event_seconds(cl_event e)
{
    cl_ulong start = 0, end = 0;
    clGetEventProfilingInfo(e, CL_PROFILING_COMMAND_START, sizeof(start), &start, NULL);
    clGetEventProfilingInfo(e, CL_PROFILING_COMMAND_END, sizeof(end), &end, NULL);
    return (double)(end - start) * 1e-9;
}

static uint64_t load_be64(const unsigned char *p)
{
    uint64_t v = 0;
    int i;
    for (i = 0; i < 8; ++i) {
        v = (v << 8) | p[i];
    }
    return v;
}

c01_ctr_status c01_gpu_aes128_ctr(
    c01_gpu *g,
    const unsigned char key[C01_AES128_KEY_SIZE],
    const unsigned char initial_counter[C01_AES_BLOCK_SIZE],
    uint64_t block_offset,
    const unsigned char *input,
    unsigned char *output,
    size_t length,
    size_t batch,
    c01_gpu_times *times)
{
    uint64_t hi, lo, nblocks, last_lo;
    cl_uint rk_words[44];
    cl_mem rk = NULL, d_in = NULL, d_out = NULL;
    size_t cap, pos;
    cl_int err;
    c01_ctr_status status = C01_CTR_CRYPTO_ERROR;

    if (times != NULL) {
        memset(times, 0, sizeof(*times));
    }
    if (g == NULL || key == NULL || initial_counter == NULL ||
        (length != 0 && (input == NULL || output == NULL)) || batch % C01_AES_BLOCK_SIZE != 0) {
        return C01_CTR_INVALID_ARGUMENT;
    }
    if (length == 0) {
        return C01_CTR_OK;
    }

    hi = load_be64(initial_counter);
    lo = load_be64(initial_counter + 8);
    lo += block_offset;
    if (lo < block_offset) {
        if (++hi == 0) {
            return C01_CTR_COUNTER_OVERFLOW;
        }
    }
    nblocks = (length + 15) / 16;
    last_lo = lo + (nblocks - 1);
    if (last_lo < lo && hi == UINT64_MAX) {
        return C01_CTR_COUNTER_OVERFLOW;
    }

    if (batch == 0) {
        batch = (size_t)nblocks * 16;
    }
    cap = batch < length ? batch : length;

    expand_key(key, rk_words);
    rk = clCreateBuffer(g->context, CL_MEM_READ_ONLY | CL_MEM_COPY_HOST_PTR, sizeof(rk_words), rk_words, &err);
    if (err != CL_SUCCESS) goto cleanup;
    d_in = clCreateBuffer(g->context, CL_MEM_READ_ONLY, cap, NULL, &err);
    if (err != CL_SUCCESS) goto cleanup;
    d_out = clCreateBuffer(g->context, CL_MEM_WRITE_ONLY, cap, NULL, &err);
    if (err != CL_SUCCESS) goto cleanup;

    for (pos = 0; pos < length; pos += batch) {
        size_t m = length - pos < batch ? length - pos : batch;
        cl_ulong len_arg = m, off_arg = pos / 16, hi_arg = hi, lo_arg = lo;
        size_t global = ((m + 15) / 16 + LOCAL_SIZE - 1) / LOCAL_SIZE * LOCAL_SIZE;
        size_t local = LOCAL_SIZE;
        cl_event ev[3];

        err = clEnqueueWriteBuffer(g->queue, d_in, CL_FALSE, 0, m, input + pos, 0, NULL, &ev[0]);
        err |= clSetKernelArg(g->kernel, 0, sizeof(cl_mem), &d_in);
        err |= clSetKernelArg(g->kernel, 1, sizeof(cl_mem), &d_out);
        err |= clSetKernelArg(g->kernel, 2, sizeof(cl_ulong), &len_arg);
        err |= clSetKernelArg(g->kernel, 3, sizeof(cl_ulong), &off_arg);
        err |= clSetKernelArg(g->kernel, 4, sizeof(cl_ulong), &hi_arg);
        err |= clSetKernelArg(g->kernel, 5, sizeof(cl_ulong), &lo_arg);
        err |= clSetKernelArg(g->kernel, 6, sizeof(cl_mem), &rk);
        err |= clSetKernelArg(g->kernel, 7, sizeof(cl_mem), &g->te0);
        err |= clSetKernelArg(g->kernel, 8, sizeof(cl_mem), &g->sbox);
        err |= clEnqueueNDRangeKernel(g->queue, g->kernel, 1, NULL, &global, &local, 0, NULL, &ev[1]);
        err |= clEnqueueReadBuffer(g->queue, d_out, CL_TRUE, 0, m, output + pos, 0, NULL, &ev[2]);
        if (err != CL_SUCCESS) goto cleanup;

        if (times != NULL) {
            times->h2d += event_seconds(ev[0]);
            times->kernel += event_seconds(ev[1]);
            times->d2h += event_seconds(ev[2]);
        }
        clReleaseEvent(ev[0]);
        clReleaseEvent(ev[1]);
        clReleaseEvent(ev[2]);
    }
    status = C01_CTR_OK;

cleanup:
    if (d_out) clReleaseMemObject(d_out);
    if (d_in) clReleaseMemObject(d_in);
    if (rk) clReleaseMemObject(rk);
    return status;
}
