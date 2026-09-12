#pragma once

#include "../../../types.h"

// The one, single embedded HMAC key for exec_sign.h's signature scheme -
// this file is #included by BOTH the kernel build and tools/sign_exec.c's
// plain host-gcc build, so signer and verifier always share the exact
// same key (never two hand-copied constants that could drift out of
// sync). Generated once with a real CSPRNG at development time, not
// hand-picked/derived from anything guessable.
//
// Honest scope, stated plainly (same spirit as kernel/lib/rand.h's own
// "not a security-grade secrecy requirement" note): this is a SYMMETRIC
// key baked into a plaintext, world-readable kernel.elf - it protects
// against corruption and naive "plant an arbitrary unsigned .bin on
// writable storage" attempts (the actual, real threat model for a
// single-machine hobby OS with no external CA/multi-party trust need),
// NOT against an attacker who has already extracted kernel.elf and this
// key from it - that would need real asymmetric signing (RSA/ECDSA),
// which needs hand-written bignum/modular exponentiation and is
// explicitly out of scope for this item (see kernel/security/exec_sign/
// exec_sign.h's own comment). The kernel is both the sole legitimate
// signer and the sole verifier here - there is no second party this key
// needs to keep a secret from in the threat model this item addresses.
#define EXEC_SIGNING_KEY_LEN 32
static const u8 EXEC_SIGNING_KEY[EXEC_SIGNING_KEY_LEN] = {
    0x26, 0xc3, 0x14, 0xa3, 0x85, 0xcd, 0x1b, 0xf2,
    0xf1, 0x79, 0x59, 0xdb, 0x49, 0x84, 0x69, 0x78,
    0xf0, 0xac, 0xcf, 0x9a, 0xf5, 0xc9, 0x44, 0xc4,
    0x75, 0x4c, 0x7d, 0xe6, 0xea, 0x0a, 0xe2, 0x9a
};
