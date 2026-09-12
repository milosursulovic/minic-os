#pragma once

#include "../../../types.h"

// Signed-executable header format (Faza I point 14, item 17). Every
// executable image loaded from writable storage (proc/process.c's
// spawn_process_from_path(), and register_service - kernel/syscall/
// handlers/process.c syscall 14) must carry this header, verified
// against kernel/security/exec_sign/signing_key.h's single embedded HMAC
// key before spawn_process() ever runs a byte of it. Builtin images
// (baked into kernel.elf at link time via each app's own _blob.s -
// desktop_shell/terminal/etc, spawned directly by kmain.c) are
// deliberately NOT signed or checked - already trusted by construction,
// not reachable through any writable path.
//
// This is a real, hand-written HMAC-SHA256 (kernel/security/hash/) check,
// not asymmetric signing - see signing_key.h for the honest statement of
// what threat this does and doesn't cover.

#pragma GCC visibility push(hidden)

#define EXEC_SIGN_MAGIC 0x314e4753u  // "SGN1", little-endian in the header's first 4 bytes
#define EXEC_SIGN_HMAC_LEN 32

// Struct layout doubles as the on-disk wire format directly (no manual
// byte-serialization step anywhere) - safe on every target this whole
// codebase ever builds for (x86/x86-64, native little-endian; u32/u8[32]/
// u32 needs no compiler padding on this ABI, so sizeof(exec_sign_header)
// really is exactly 4+32+4=40 bytes) - same "assume x86, state it
// plainly" spirit as every other x86-specific assumption already baked
// into this codebase (see CLAUDE.md's own 32-bit-ELF-container notes).
typedef struct {
    u32 magic;
    u8 hmac[EXEC_SIGN_HMAC_LEN];
    u32 payload_len;
} exec_sign_header;

// Verifies `signed_image[0..signed_len)` as [header][payload]. On success,
// returns true and sets *payload_out/*payload_len_out to the verified
// payload range (still inside signed_image, no copy). On any failure
// (too short, bad magic, HMAC mismatch, payload_len not matching what's
// actually present), returns false and leaves the output pointers alone.
bool exec_sign_verify(const u8* signed_image, u32 signed_len,
                       const u8** payload_out, u32* payload_len_out);

// The same primitive run forward: fills in a fresh, correct header for a
// given payload. Used both by tools/sign_exec.c's host-side signing tool
// and in-kernel by shell/shell/commands/process.c's cmd_install() to
// self-sign the one builtin demo blob it writes out to the VFS.
void exec_sign_produce(const u8* payload, u32 payload_len, exec_sign_header* header_out);

#pragma GCC visibility pop
