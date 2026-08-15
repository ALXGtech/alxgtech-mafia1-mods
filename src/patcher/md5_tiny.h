/* md5_tiny.h - md5-of-a-file, over the shared arithmetic in src\shared\md5_core.h.
 *
 * Moved out of patcher.c on 2026-08-01 unchanged, because journal.h needs the same hash to
 * decide whether a file is still the one we wrote. Two copies of a hash is how two answers to
 * "is this ours" appear in one program.
 *
 * The arithmetic MOVED OUT on 2026-08-14, to src\shared\md5_core.h, and this file kept only the
 * file-streaming half. Same reason as the sentence above, one scope wider: the two .asi modules
 * now hash as well, they are linked -nostdlib, and the version of md5_update that lived here
 * called memcpy. One copy of the arithmetic, two front ends - this one for programs that have a
 * C library, the core alone for the ones that do not.
 *
 * THE PROVENANCE STATEMENT MOVED WITH THE ARITHMETIC, to md5_core.h, on 2026-08-14. It used to
 * sit here and say "this is the reference implementation's arithmetic, written out" - which
 * described code that is no longer in this file, while the file that does hold it said nothing.
 * A licence claim attached to the wrong file is worse than none: it reads as answered.
 * What is left here is file I/O over that header's functions, and it asserts nothing.
 *
 * Md5OfFile streams the file in 64 KB chunks rather than reading it whole. dinput8.dll is
 * 2.1 MB and Game.exe 2.3 MB, and a hash function that allocates is one that can fail for a
 * reason having nothing to do with hashing.
 */
#ifndef ALXG_MD5_TINY_H
#define ALXG_MD5_TINY_H

#include <stdio.h>
#include <string.h>

#include "../shared/md5_core.h"

/* 1 on success, 0 if the file cannot be opened. An empty file hashes correctly rather than
   failing: d41d8cd98f00b204e9800998ecf8427e is a real answer, and treating it as an error
   would make an empty file indistinguishable from a missing one. */
static int Md5OfFile(const char *path, char out[33])
{
    unsigned char chunk[65536];
    md5_ctx c;
    FILE *f = fopen(path, "rb");
    size_t got;
    if (!f) return 0;
    md5_init(&c);
    while ((got = fread(chunk, 1, sizeof chunk, f)) > 0) md5_update(&c, chunk, got);
    fclose(f);
    md5_final(&c, out);
    return 1;
}

#endif /* ALXG_MD5_TINY_H */
