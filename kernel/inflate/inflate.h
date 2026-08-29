#pragma once

#include "../../types.h"

#pragma GCC visibility push(hidden)

// Hand-written DEFLATE (RFC 1951) decompressor - the only compression
// primitive in this kernel. Genuinely generic (no PNG knowledge at all);
// kernel/gfx/png/png.c is its only caller today, but it lives at this
// top level, alongside isr/sched/fs/gfx/net, because decompression isn't
// a graphics concern.
//
// Decompresses src (src_len bytes of raw DEFLATE-compressed data, e.g. a
// zlib stream with its 2-byte header/4-byte Adler32 trailer already
// stripped by the caller) into dst (caller-owned, dst_cap bytes) and
// writes the real decompressed length to *out_len. Returns false on any
// malformed input - truncated stream, reserved block type, a
// length/distance code the two RFC tables don't cover, a back-reference
// reaching before the start of output, or output that would overflow
// dst_cap - never guesses or silently truncates.
bool inflate(const u8* src, u32 src_len, u8* dst, u32 dst_cap, u32* out_len);

#pragma GCC visibility pop
