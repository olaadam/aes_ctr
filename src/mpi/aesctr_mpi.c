#include "c01/ctr.h"

#include <mpi.h>
#include <errno.h>
#include <inttypes.h>
#include <limits.h>
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

static void usage(FILE *f)
{
    fprintf(f, "Usage: aesctr_mpi <in> <out> --key <32-hex> --ctr <32-hex> "
               "--buf <bytes> --mode {serial|omp}\n");
}

static int parse_hex(const char *s, unsigned char *out, size_t n)
{
    size_t i;
    if (!s || strlen(s) != 2 * n) return 0;
    for (i = 0; i < n; ++i) {
        unsigned int value;
        char tail;
        if (sscanf(s + 2 * i, "%2x%c", &value, &tail) != 1 || value > 255) return 0;
        out[i] = (unsigned char)value;
    }
    return 1;
}

static int parse_size(const char *s, uint64_t *out)
{
    char *end = NULL;
    unsigned long long value;
    errno = 0;
    value = strtoull(s, &end, 10);
    if (errno || end == s || *end || value < 16) return 0;
    *out = (uint64_t)value;
    return 1;
}

/* Add a global block index to a 128-bit big-endian CTR value. */
static int counter_add_blocks(const unsigned char start[16], uint64_t blocks,
                              unsigned char result[16])
{
    uint64_t carry = blocks;
    int i;
    memcpy(result, start, 16);
    for (i = 15; i >= 0 && carry != 0; --i) {
        uint64_t sum = (uint64_t)result[i] + (carry & 0xffu);
        result[i] = (unsigned char)sum;
        carry = (carry >> 8) + (sum >> 8);
    }
    return carry == 0;
}

static void mpi_fail(int rank, const char *what, int code)
{
    fprintf(stderr, "rank %d: %s (MPI/code %d)\n", rank, what, code);
    MPI_Abort(MPI_COMM_WORLD, code ? code : 1);
}

int main(int argc, char **argv)
{
    int rank, ranks, i, valid = 1, mode = 0, all_valid;
    const char *input_path = NULL, *output_path = NULL, *key_hex = NULL, *ctr_hex = NULL;
    const char *mode_text = NULL;
    unsigned char key[16] = {0}, ctr0[16] = {0}, last_ctr[16];
    uint64_t buffer_size = 0, wire_buffer_size = 0;
    uint64_t total_blocks, local_blocks, first_block, quotient, remainder;
    uint64_t local_bytes, done = 0, cap;
    MPI_Offset file_size, file_offset;
    MPI_File in_file, out_file;
    unsigned char *inbuf = NULL, *outbuf = NULL;
    double distribution_start, distribution_local, distribution_max;
    double pipeline_start, pipeline_local, pipeline_max;
    double read_seconds = 0, crypto_seconds = 0, write_seconds = 0;
    double phase[3], phase_max[3];
    int io_error = MPI_SUCCESS;

    MPI_Init(&argc, &argv);
    MPI_Comm_rank(MPI_COMM_WORLD, &rank);
    MPI_Comm_size(MPI_COMM_WORLD, &ranks);
    MPI_Comm_set_errhandler(MPI_COMM_WORLD, MPI_ERRORS_RETURN);

    if (argc < 3) valid = 0;
    else {
        input_path = argv[1]; output_path = argv[2];
        for (i = 3; i < argc; ++i) {
            if (i + 1 >= argc) { valid = 0; break; }
            if (!strcmp(argv[i], "--key")) key_hex = argv[++i];
            else if (!strcmp(argv[i], "--ctr")) ctr_hex = argv[++i];
            else if (!strcmp(argv[i], "--buf")) {
                if (!parse_size(argv[++i], &buffer_size)) valid = 0;
            } else if (!strcmp(argv[i], "--mode")) mode_text = argv[++i];
            else valid = 0;
        }
        if (!key_hex || !ctr_hex || !mode_text ||
            !parse_hex(key_hex, key, sizeof(key)) || !parse_hex(ctr_hex, ctr0, sizeof(ctr0)))
            valid = 0;
        if (mode_text && !strcmp(mode_text, "serial")) mode = 0;
        else if (mode_text && !strcmp(mode_text, "omp")) mode = 1;
        else valid = 0;
        if (input_path && output_path && !strcmp(input_path, output_path)) valid = 0;
    }
    MPI_Allreduce(&valid, &all_valid, 1, MPI_INT, MPI_MIN, MPI_COMM_WORLD);
    if (!all_valid) {
        if (rank == 0) { usage(stderr); fprintf(stderr, "Invalid arguments; input and output paths must differ.\n"); }
        MPI_Finalize();
        return 2;
    }

    /* Treat the root's configuration broadcast as the MPI distribution phase. */
    wire_buffer_size = buffer_size;
    MPI_Barrier(MPI_COMM_WORLD);
    distribution_start = MPI_Wtime();
    if (MPI_Bcast(key, 16, MPI_BYTE, 0, MPI_COMM_WORLD) != MPI_SUCCESS ||
        MPI_Bcast(ctr0, 16, MPI_BYTE, 0, MPI_COMM_WORLD) != MPI_SUCCESS ||
        MPI_Bcast(&wire_buffer_size, 1, MPI_UINT64_T, 0, MPI_COMM_WORLD) != MPI_SUCCESS ||
        MPI_Bcast(&mode, 1, MPI_INT, 0, MPI_COMM_WORLD) != MPI_SUCCESS)
        mpi_fail(rank, "configuration broadcast failed", 1);
    distribution_local = MPI_Wtime() - distribution_start;
    MPI_Reduce(&distribution_local, &distribution_max, 1, MPI_DOUBLE, MPI_MAX, 0, MPI_COMM_WORLD);
    buffer_size = wire_buffer_size;

    pipeline_start = MPI_Wtime();
    if (MPI_File_open(MPI_COMM_WORLD, input_path, MPI_MODE_RDONLY, MPI_INFO_NULL, &in_file) != MPI_SUCCESS)
        mpi_fail(rank, "cannot open input", 1);
    if (MPI_File_open(MPI_COMM_WORLD, output_path, MPI_MODE_CREATE | MPI_MODE_WRONLY,
                      MPI_INFO_NULL, &out_file) != MPI_SUCCESS)
        mpi_fail(rank, "cannot open output", 1);
    MPI_File_set_errhandler(in_file, MPI_ERRORS_RETURN);
    MPI_File_set_errhandler(out_file, MPI_ERRORS_RETURN);
    if (MPI_File_get_size(in_file, &file_size) != MPI_SUCCESS || file_size < 0)
        mpi_fail(rank, "cannot determine input size", 1);

    total_blocks = (uint64_t)(file_size / 16) + ((file_size % 16) != 0);
    if (total_blocks != 0 && !counter_add_blocks(ctr0, total_blocks - 1, last_ctr)) {
        if (rank == 0) fprintf(stderr, "CTR counter range exceeds 128 bits\n");
        MPI_Abort(MPI_COMM_WORLD, 2);
    }
    quotient = total_blocks / (uint64_t)ranks;
    remainder = total_blocks % (uint64_t)ranks;
    local_blocks = quotient + ((uint64_t)rank < remainder);
    first_block = (uint64_t)rank * quotient + ((uint64_t)rank < remainder ? (uint64_t)rank : remainder);
    file_offset = (MPI_Offset)(first_block * 16);
    if (local_blocks == 0) local_bytes = 0;
    else if (first_block + local_blocks == total_blocks)
        local_bytes = (uint64_t)file_size - (uint64_t)file_offset;
    else local_bytes = local_blocks * 16;

    if (MPI_File_set_size(out_file, file_size) != MPI_SUCCESS)
        mpi_fail(rank, "cannot size output", 1);
    cap = buffer_size - buffer_size % 16;
    if (cap > (uint64_t)(INT_MAX - (INT_MAX % 16))) cap = (uint64_t)(INT_MAX - (INT_MAX % 16));
    if (cap == 0 || cap > SIZE_MAX) {
        if (rank == 0) fprintf(stderr, "Buffer size must be at least 16 and fit address space\n");
        MPI_Abort(MPI_COMM_WORLD, 2);
    }
    inbuf = (unsigned char *)malloc((size_t)cap);
    outbuf = (unsigned char *)malloc((size_t)cap);
    if (!inbuf || !outbuf) { fprintf(stderr, "rank %d: buffer allocation failed\n", rank); MPI_Abort(MPI_COMM_WORLD, 2); }

    MPI_Barrier(MPI_COMM_WORLD);
    while (done < local_bytes) {
        uint64_t amount = local_bytes - done < cap ? local_bytes - done : cap;
        int count = (int)amount, actual = 0;
        MPI_Status mpi_status;
        unsigned char chunk_ctr[16];
        uint64_t global_block = first_block + done / 16;
        c01_ctr_status crypto_status;
        double t0;

        t0 = MPI_Wtime();
        io_error = MPI_File_read_at(in_file, file_offset + (MPI_Offset)done,
                                    inbuf, count, MPI_BYTE, &mpi_status);
        if (io_error == MPI_SUCCESS) MPI_Get_count(&mpi_status, MPI_BYTE, &actual);
        read_seconds += MPI_Wtime() - t0;
        if (io_error != MPI_SUCCESS || actual != count) mpi_fail(rank, "short/error input read", io_error);

        if (!counter_add_blocks(ctr0, global_block, chunk_ctr))
            mpi_fail(rank, "counter range overflow for rank chunk", 2);
        t0 = MPI_Wtime();
        crypto_status = mode ? c01_aes128_ctr_transform_omp(key, chunk_ctr, inbuf, outbuf, (size_t)amount)
                             : c01_aes128_ctr_transform(key, chunk_ctr, inbuf, outbuf, (size_t)amount);
        crypto_seconds += MPI_Wtime() - t0;
        if (crypto_status != C01_CTR_OK) {
            fprintf(stderr, "rank %d: CTR error: %s\n", rank, c01_ctr_status_string(crypto_status));
            MPI_Abort(MPI_COMM_WORLD, 2);
        }

        t0 = MPI_Wtime();
        io_error = MPI_File_write_at(out_file, file_offset + (MPI_Offset)done,
                                     outbuf, count, MPI_BYTE, &mpi_status);
        if (io_error == MPI_SUCCESS) MPI_Get_count(&mpi_status, MPI_BYTE, &actual);
        write_seconds += MPI_Wtime() - t0;
        if (io_error != MPI_SUCCESS || actual != count) mpi_fail(rank, "short/error output write", io_error);
        done += amount;
    }

    if (MPI_File_sync(out_file) != MPI_SUCCESS) mpi_fail(rank, "output sync failed", 1);
    free(inbuf); free(outbuf);
    {
        int close_in = MPI_File_close(&in_file);
        int close_out = MPI_File_close(&out_file);
        if (close_in != MPI_SUCCESS || close_out != MPI_SUCCESS)
            mpi_fail(rank, "file close failed", 1);
    }
    pipeline_local = MPI_Wtime() - pipeline_start;
    phase[0] = read_seconds; phase[1] = crypto_seconds; phase[2] = write_seconds;
    MPI_Reduce(&pipeline_local, &pipeline_max, 1, MPI_DOUBLE, MPI_MAX, 0, MPI_COMM_WORLD);
    MPI_Reduce(phase, phase_max, 3, MPI_DOUBLE, MPI_MAX, 0, MPI_COMM_WORLD);

    if (rank == 0) {
        double gbps = pipeline_max > 0 ? (double)file_size / pipeline_max / 1e9 : 0;
        printf("MPI ranks: %d, buffer: %" PRIu64 " B, file: %" PRIu64 " B, mode: %s\n",
               ranks, buffer_size, (uint64_t)file_size, mode ? "OpenMP" : "serial");
        printf("Effective per-rank chunk buffer: %" PRIu64 " B\n", cap);
        printf("Config distribution (MPI_Bcast): %.6f s\n", distribution_max);
        printf("Data distribution/read (MPI_File_read_at, max rank): %.6f s\n", phase_max[0]);
        printf("Pipeline max-rank time: %.6f s (%.4f GB/s)\n", pipeline_max, gbps);
        printf("Max-rank stages: read %.6f s, crypto %.6f s, write %.6f s\n",
               phase_max[0], phase_max[1], phase_max[2]);
    }
    MPI_Finalize();
    return 0;
}
