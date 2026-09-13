#pragma once
#include "../../../types.h"

#pragma GCC visibility push(hidden)

// Hand-written fixed-capacity unsigned big integer - big-endian on the wire
// (matches RSA moduli / PKCS#1 data), little-endian limb order internally.
// Sized for up to 4096-bit values (headroom over a real 2048-bit RSA key).
// See kernel/net/tls/tls.c's own comment for why this is enough: the
// kernel only ever RSA-*encrypts* with a server's small public exponent
// (never a private-key operation), so bignum_modexp's cost tracks the
// exponent's bit length (~17 for e=65537), not the modulus's.
#define BIGNUM_LIMBS 128  // 128 * 32 bits = 4096 bits

typedef struct {
    u32 limb[BIGNUM_LIMBS];  // limb[0] = least significant
} bignum_t;

void bignum_zero(bignum_t* n);
void bignum_set_u32(bignum_t* n, u32 value);
void bignum_copy(bignum_t* dst, const bignum_t* src);

// Big-endian byte import/export - `len` must be <= BIGNUM_LIMBS*4.
void bignum_from_bytes_be(bignum_t* out, const u8* bytes, int len);
void bignum_to_bytes_be(const bignum_t* n, u8* out, int len);

// Returns -1/0/1 for a<b / a==b / a>b.
int bignum_compare(const bignum_t* a, const bignum_t* b);

// Number of significant bits (0 for n==0) - e.g. a real 2048-bit RSA
// modulus (top bit always set by construction) returns exactly 2048,
// letting a caller derive its exact byte length: (bignum_bit_length(n)+7)/8.
int bignum_bit_length(const bignum_t* n);

// result = a - b. Caller must ensure a >= b (the only case bignum_modexp's
// internal reduction ever needs).
void bignum_sub(bignum_t* result, const bignum_t* a, const bignum_t* b);

// result = (a * b) mod m.
void bignum_mulmod(bignum_t* result, const bignum_t* a, const bignum_t* b, const bignum_t* mod);

// result = (base ^ exp) mod m, via square-and-multiply.
void bignum_modexp(bignum_t* result, const bignum_t* base, const bignum_t* exp, const bignum_t* mod);

#pragma GCC visibility pop
