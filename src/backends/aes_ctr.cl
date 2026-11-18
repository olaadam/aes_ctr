inline uint bswap32(uint x)
{
    return rotate(x & 0x00ff00ffu, 24u) | rotate(x & 0xff00ff00u, 8u);
}

__kernel void aes128_ctr(__global const uchar *in,
                         __global uchar *out,
                         ulong len,
                         ulong block_offset,
                         ulong ctr_hi,
                         ulong ctr_lo,
                         __constant uint *rk,
                         __constant uint *te0_src,
                         __constant uchar *sbox_src)
{
    __local uint te0[256];
    __local uchar sbox[256];

    for (uint i = get_local_id(0); i < 256; i += get_local_size(0)) {
        te0[i] = te0_src[i];
        sbox[i] = sbox_src[i];
    }
    barrier(CLK_LOCAL_MEM_FENCE);

    ulong gid = get_global_id(0);
    ulong offset = gid * 16;
    if (offset >= len) {
        return;
    }

    ulong b = block_offset + gid;
    ulong lo = ctr_lo + b;
    ulong hi = ctr_hi + (lo < ctr_lo ? 1 : 0);

    uint s0 = (uint)(hi >> 32) ^ rk[0];
    uint s1 = (uint)hi ^ rk[1];
    uint s2 = (uint)(lo >> 32) ^ rk[2];
    uint s3 = (uint)lo ^ rk[3];
    uint t0, t1, t2, t3;

    for (int r = 1; r < 10; ++r) {
        __constant uint *k = rk + 4 * r;
        t0 = te0[s0 >> 24] ^ rotate(te0[(s1 >> 16) & 0xff], 24u) ^ rotate(te0[(s2 >> 8) & 0xff], 16u) ^ rotate(te0[s3 & 0xff], 8u) ^ k[0];
        t1 = te0[s1 >> 24] ^ rotate(te0[(s2 >> 16) & 0xff], 24u) ^ rotate(te0[(s3 >> 8) & 0xff], 16u) ^ rotate(te0[s0 & 0xff], 8u) ^ k[1];
        t2 = te0[s2 >> 24] ^ rotate(te0[(s3 >> 16) & 0xff], 24u) ^ rotate(te0[(s0 >> 8) & 0xff], 16u) ^ rotate(te0[s1 & 0xff], 8u) ^ k[2];
        t3 = te0[s3 >> 24] ^ rotate(te0[(s0 >> 16) & 0xff], 24u) ^ rotate(te0[(s1 >> 8) & 0xff], 16u) ^ rotate(te0[s2 & 0xff], 8u) ^ k[3];
        s0 = t0; s1 = t1; s2 = t2; s3 = t3;
    }

    __constant uint *k = rk + 40;
    t0 = ((uint)sbox[s0 >> 24] << 24) | ((uint)sbox[(s1 >> 16) & 0xff] << 16) | ((uint)sbox[(s2 >> 8) & 0xff] << 8) | sbox[s3 & 0xff];
    t1 = ((uint)sbox[s1 >> 24] << 24) | ((uint)sbox[(s2 >> 16) & 0xff] << 16) | ((uint)sbox[(s3 >> 8) & 0xff] << 8) | sbox[s0 & 0xff];
    t2 = ((uint)sbox[s2 >> 24] << 24) | ((uint)sbox[(s3 >> 16) & 0xff] << 16) | ((uint)sbox[(s0 >> 8) & 0xff] << 8) | sbox[s1 & 0xff];
    t3 = ((uint)sbox[s3 >> 24] << 24) | ((uint)sbox[(s0 >> 16) & 0xff] << 16) | ((uint)sbox[(s1 >> 8) & 0xff] << 8) | sbox[s2 & 0xff];

    uchar16 ks = as_uchar16((uint4)(bswap32(t0 ^ k[0]), bswap32(t1 ^ k[1]),
                                    bswap32(t2 ^ k[2]), bswap32(t3 ^ k[3])));

    ulong n = len - offset;
    if (n >= 16) {
        vstore16(vload16(gid, in) ^ ks, gid, out);
    } else {
        uchar tmp[16];
        vstore16(ks, 0, tmp);
        for (uint i = 0; i < n; ++i) {
            out[offset + i] = in[offset + i] ^ tmp[i];
        }
    }
}
