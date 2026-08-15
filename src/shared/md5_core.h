/* md5_core.h - RFC 1321 md5 with NO library dependency at all.
 *
 * Split out of src\patcher\md5_tiny.h on 2026-08-14 so the two .asi modules can hash as well as
 * the two .exe can. The reason is not tidiness:
 *
 *   - the .asi are linked `-nostdlib` against kernel32 and user32 only, so anything that reaches
 *     for memcpy, sprintf or stdio does not fail to compile - it fails to LINK, or worse, links
 *     against a builtin that -O2 decided to emit a call to (see tools\build-fp.ps1's -fno-builtin
 *     note, which was paid for);
 *   - md5_tiny.h uses memcpy in md5_update and sprintf in md5_final, so it could not be included
 *     there as it stood.
 *
 * So the arithmetic lives here, written with explicit byte loops and its own hex writer, and
 * md5_tiny.h now includes this file and adds only the file-streaming half. There is still exactly
 * ONE copy of the arithmetic in the project, which is the property md5_tiny.h's own header comment
 * asked for.
 *
 * WHY THE .asi NEEDED A HASH AT ALL. Both modules recognise the build they are about to patch by
 * comparing a short run of the game's own machine code for equality. Storing that run verbatim
 * means publishing the game's code in our repository, however few bytes it is. A digest has the
 * identical safety property - a build whose code at the site differs still fails to match, and
 * still disables the feature rather than guessing - and carries none of their bytes.
 *
 * Lengths are `unsigned int` rather than size_t on purpose: size_t needs <stddef.h>, and every
 * caller here hashes tens of bytes or 64 KB chunks. Everything in this project is -m32.
 *
 * PROVENANCE, and it belongs on THIS file because this is where the arithmetic now lives.
 * md5 is specified in RFC 1321, which places the algorithm in the public domain and permits
 * anyone to implement it; what follows is an implementation of that specification, written out
 * from the RFC's own description of the four rounds - the K table is the published
 * floor(2^32 * |sin(i)|) series, the shift table is the RFC's, and the padding and length
 * encoding are the RFC's. No code was copied from RSA's reference implementation or from any
 * other codebase, so the licence terms attached to those do not reach this file.
 * It is verified against the RFC's own test vectors in the project's offline harness, which is
 * also the check that this is md5 and not something that looks like it.
 */
#ifndef ALXG_MD5_CORE_H
#define ALXG_MD5_CORE_H

typedef struct {
    unsigned int a, b, c, d;
    unsigned long long len;
    unsigned char buf[64];
    unsigned int n;
} md5_ctx;

static unsigned int md5_rol(unsigned int x, int c) { return (x << c) | (x >> (32 - c)); }

static void md5_block(md5_ctx *ctx, const unsigned char *p)
{
    static const unsigned int K[64] = {
        0xd76aa478,0xe8c7b756,0x242070db,0xc1bdceee,0xf57c0faf,0x4787c62a,0xa8304613,0xfd469501,
        0x698098d8,0x8b44f7af,0xffff5bb1,0x895cd7be,0x6b901122,0xfd987193,0xa679438e,0x49b40821,
        0xf61e2562,0xc040b340,0x265e5a51,0xe9b6c7aa,0xd62f105d,0x02441453,0xd8a1e681,0xe7d3fbc8,
        0x21e1cde6,0xc33707d6,0xf4d50d87,0x455a14ed,0xa9e3e905,0xfcefa3f8,0x676f02d9,0x8d2a4c8a,
        0xfffa3942,0x8771f681,0x6d9d6122,0xfde5380c,0xa4beea44,0x4bdecfa9,0xf6bb4b60,0xbebfbc70,
        0x289b7ec6,0xeaa127fa,0xd4ef3085,0x04881d05,0xd9d4d039,0xe6db99e5,0x1fa27cf8,0xc4ac5665,
        0xf4292244,0x432aff97,0xab9423a7,0xfc93a039,0x655b59c3,0x8f0ccc92,0xffeff47d,0x85845dd1,
        0x6fa87e4f,0xfe2ce6e0,0xa3014314,0x4e0811a1,0xf7537e82,0xbd3af235,0x2ad7d2bb,0xeb86d391 };
    static const int R[64] = {
        7,12,17,22,7,12,17,22,7,12,17,22,7,12,17,22,
        5, 9,14,20,5, 9,14,20,5, 9,14,20,5, 9,14,20,
        4,11,16,23,4,11,16,23,4,11,16,23,4,11,16,23,
        6,10,15,21,6,10,15,21,6,10,15,21,6,10,15,21 };
    unsigned int M[16], A = ctx->a, B = ctx->b, C = ctx->c, D = ctx->d, i;
    for (i = 0; i < 16; i++)
        M[i] = (unsigned int)p[i*4] | ((unsigned int)p[i*4+1] << 8) |
               ((unsigned int)p[i*4+2] << 16) | ((unsigned int)p[i*4+3] << 24);
    for (i = 0; i < 64; i++) {
        unsigned int F; int g;
        if (i < 16)      { F = (B & C) | (~B & D);        g = i; }
        else if (i < 32) { F = (D & B) | (~D & C);        g = (5*i + 1) & 15; }
        else if (i < 48) { F = B ^ C ^ D;                 g = (3*i + 5) & 15; }
        else             { F = C ^ (B | ~D);              g = (7*i) & 15; }
        F += A + K[i] + M[g];
        A = D; D = C; C = B; B += md5_rol(F, R[i]);
    }
    ctx->a += A; ctx->b += B; ctx->c += C; ctx->d += D;
}

static void md5_init(md5_ctx *c)
{
    c->a = 0x67452301; c->b = 0xefcdab89; c->c = 0x98badcfe; c->d = 0x10325476;
    c->len = 0; c->n = 0;
}

/* The byte loop is deliberate and must not be "tidied" back into memcpy: a memcpy here is a call
   into a library the .asi does not link. At 64 bytes a copy it costs nothing measurable. */
static void md5_update(md5_ctx *c, const void *data, unsigned int len)
{
    const unsigned char *p = (const unsigned char *)data;
    c->len += (unsigned long long)len;
    while (len) {
        unsigned int take = 64 - c->n, i;
        if (take > len) take = len;
        for (i = 0; i < take; i++) c->buf[c->n + i] = p[i];
        c->n += take; p += take; len -= take;
        if (c->n == 64) { md5_block(c, c->buf); c->n = 0; }
    }
}

/* Lower-case hex, NUL terminated - the same 33-byte form every identity constant in this project
   is written in, so a digest can be compared with a plain string compare and read in a log. */
static void md5_final(md5_ctx *c, char out[33])
{
    static const char hexd[16] = { '0','1','2','3','4','5','6','7',
                                   '8','9','a','b','c','d','e','f' };
    unsigned long long bits = c->len * 8;
    unsigned char pad = 0x80;
    unsigned char lenb[8];
    unsigned int i;
    unsigned int words[4];
    md5_update(c, &pad, 1);
    while (c->n != 56) { unsigned char z = 0; md5_update(c, &z, 1); }
    for (i = 0; i < 8; i++) lenb[i] = (unsigned char)(bits >> (8 * i));
    md5_update(c, lenb, 8);
    words[0] = c->a; words[1] = c->b; words[2] = c->c; words[3] = c->d;
    for (i = 0; i < 16; i++) {
        unsigned char byte = (unsigned char)(words[i / 4] >> (8 * (i % 4)));
        out[i * 2]     = hexd[byte >> 4];
        out[i * 2 + 1] = hexd[byte & 15];
    }
    out[32] = 0;
}

static void md5_bytes(const void *p, unsigned int n, char out[33])
{
    md5_ctx c; md5_init(&c); md5_update(&c, p, n); md5_final(&c, out);
}

/* 1 when the 32 hex characters are the same. Its own loop for the same reason as above - strcmp
   is not available in a -nostdlib link. Fixed length, so there is no early-exit subtlety to get
   wrong; this compares identity constants, never a secret, so timing does not matter. */
static int md5_hex_same(const char a[33], const char *b)
{
    int i;
    for (i = 0; i < 32; i++) if (a[i] != b[i]) return 0;
    return b[32] == 0;
}

#endif /* ALXG_MD5_CORE_H */
