// Hand-written schoolbook bignum + modular exponentiation - see bignum.h's
// own comment for scope (public-exponent RSA encryption only, no library).

#include "bignum.h"

#define WIDE_LIMBS (2 * BIGNUM_LIMBS)

void bignum_zero(bignum_t* n) {
    int i = 0;
    while (i < BIGNUM_LIMBS) {
        n->limb[i] = 0;
        i = i + 1;
    }
}

void bignum_set_u32(bignum_t* n, u32 value) {
    bignum_zero(n);
    n->limb[0] = value;
}

void bignum_copy(bignum_t* dst, const bignum_t* src) {
    int i = 0;
    while (i < BIGNUM_LIMBS) {
        dst->limb[i] = src->limb[i];
        i = i + 1;
    }
}

void bignum_from_bytes_be(bignum_t* out, const u8* bytes, int len) {
    bignum_zero(out);
    // bytes[len-1] is the least significant byte; walk from the end.
    int i = 0;
    while (i < len) {
        int byte_index = len - 1 - i;   // 0 = least significant byte
        u8 b = bytes[byte_index];
        int limb_index = i / 4;
        int shift = (i % 4) * 8;
        out->limb[limb_index] = out->limb[limb_index] | (((u32) b) << shift);
        i = i + 1;
    }
}

void bignum_to_bytes_be(const bignum_t* n, u8* out, int len) {
    int i = 0;
    while (i < len) {
        int byte_index = len - 1 - i;  // 0 = least significant byte
        int limb_index = i / 4;
        int shift = (i % 4) * 8;
        out[byte_index] = (u8) ((n->limb[limb_index] >> shift) & 0xFF);
        i = i + 1;
    }
}

// --- generic limb-array helpers (operate on raw u32 arrays of length n) ---

static int cmp_limbs(const u32* a, const u32* b, int n) {
    int i = n - 1;
    while (i >= 0) {
        if (a[i] != b[i]) {
            return (a[i] > b[i]) ? 1 : -1;
        }
        i = i - 1;
    }
    return 0;
}

// r = a - b, assumes a >= b. r may alias a.
static void sub_limbs(u32* r, const u32* a, const u32* b, int n) {
    i64 borrow = 0;
    int i = 0;
    while (i < n) {
        i64 diff = (i64) a[i] - (i64) b[i] - borrow;
        if (diff < 0) {
            diff = diff + (((i64) 1) << 32);
            borrow = 1;
        } else {
            borrow = 0;
        }
        r[i] = (u32) diff;
        i = i + 1;
    }
}

// Shift the n-limb array right by exactly 1 bit, in place.
static void shr1_limbs(u32* a, int n) {
    u32 carry = 0;
    int i = n - 1;
    while (i >= 0) {
        u32 new_carry = a[i] & 1;
        a[i] = (a[i] >> 1) | (carry << 31);
        carry = new_carry;
        i = i - 1;
    }
}

// Shift the n-limb array left by `bits` (0 <= bits < n*32), in place.
static void shl_limbs(u32* a, int n, int bits) {
    int limb_shift = bits / 32;
    int bit_shift = bits % 32;
    if (limb_shift > 0) {
        int i = n - 1;
        while (i >= limb_shift) {
            a[i] = a[i - limb_shift];
            i = i - 1;
        }
        i = 0;
        while (i < limb_shift) {
            a[i] = 0;
            i = i + 1;
        }
    }
    if (bit_shift > 0) {
        u32 carry = 0;
        int i = 0;
        while (i < n) {
            u32 new_carry = a[i] >> (32 - bit_shift);
            a[i] = (a[i] << bit_shift) | carry;
            carry = new_carry;
            i = i + 1;
        }
    }
}

// Returns the index of the highest set bit (0-based), or -1 if all zero.
static int highest_bit_index(const u32* a, int n) {
    int i = n - 1;
    while (i >= 0) {
        if (a[i] != 0) {
            u32 v = a[i];
            int bit = 31;
            while (((v >> bit) & 1) == 0) {
                bit = bit - 1;
            }
            return i * 32 + bit;
        }
        i = i - 1;
    }
    return -1;
}

// out[0..an+bn) = a[0..an) * b[0..bn), full schoolbook multiply with carry
// propagation. out must be zeroed by the caller (or exactly an+bn wide).
static void mul_limbs(u32* out, const u32* a, int an, const u32* b, int bn) {
    int k = 0;
    while (k < an + bn) {
        out[k] = 0;
        k = k + 1;
    }
    int i = 0;
    while (i < an) {
        u64 carry = 0;
        int j = 0;
        while (j < bn) {
            u64 prod = ((u64) a[i]) * ((u64) b[j]) + (u64) out[i + j] + carry;
            out[i + j] = (u32) (prod & 0xFFFFFFFFu);
            carry = prod >> 32;
            j = j + 1;
        }
        int idx = i + bn;
        while (carry != 0) {
            u64 sum = (u64) out[idx] + carry;
            out[idx] = (u32) (sum & 0xFFFFFFFFu);
            carry = sum >> 32;
            idx = idx + 1;
        }
        i = i + 1;
    }
}

// wide[0..wide_n) mod mod[0..mod_n) -> out[0..mod_n), via bit-serial
// (restoring) long division. Mutates `wide` as scratch space.
static void mod_reduce_wide(u32* wide, int wide_n, const u32* mod, int mod_n, u32* out) {
    int top_rem = highest_bit_index(wide, wide_n);
    int top_mod = highest_bit_index(mod, mod_n);

    if (top_rem < 0 || top_rem < top_mod) {
        // wide is zero, or already smaller than the modulus - no reduction needed.
        int i = 0;
        while (i < mod_n) {
            out[i] = wide[i];
            i = i + 1;
        }
        return;
    }

    u32 shifted_mod[WIDE_LIMBS];
    int i = 0;
    while (i < mod_n) {
        shifted_mod[i] = mod[i];
        i = i + 1;
    }
    while (i < wide_n) {
        shifted_mod[i] = 0;
        i = i + 1;
    }

    int shift = top_rem - top_mod;
    shl_limbs(shifted_mod, wide_n, shift);

    int s = shift;
    while (s >= 0) {
        if (cmp_limbs(wide, shifted_mod, wide_n) >= 0) {
            sub_limbs(wide, wide, shifted_mod, wide_n);
        }
        if (s > 0) {
            shr1_limbs(shifted_mod, wide_n);
        }
        s = s - 1;
    }

    i = 0;
    while (i < mod_n) {
        out[i] = wide[i];
        i = i + 1;
    }
}

int bignum_compare(const bignum_t* a, const bignum_t* b) {
    return cmp_limbs(a->limb, b->limb, BIGNUM_LIMBS);
}

int bignum_bit_length(const bignum_t* n) {
    int top = highest_bit_index(n->limb, BIGNUM_LIMBS);
    if (top < 0) {
        return 0;
    }
    return top + 1;
}

void bignum_sub(bignum_t* result, const bignum_t* a, const bignum_t* b) {
    sub_limbs(result->limb, a->limb, b->limb, BIGNUM_LIMBS);
}

void bignum_mulmod(bignum_t* result, const bignum_t* a, const bignum_t* b, const bignum_t* mod) {
    u32 wide[WIDE_LIMBS];
    mul_limbs(wide, a->limb, BIGNUM_LIMBS, b->limb, BIGNUM_LIMBS);
    u32 reduced[BIGNUM_LIMBS];
    mod_reduce_wide(wide, WIDE_LIMBS, mod->limb, BIGNUM_LIMBS, reduced);
    int i = 0;
    while (i < BIGNUM_LIMBS) {
        result->limb[i] = reduced[i];
        i = i + 1;
    }
}

static int bignum_test_bit(const bignum_t* n, int bit_index) {
    int limb_index = bit_index / 32;
    int shift = bit_index % 32;
    return (int) ((n->limb[limb_index] >> shift) & 1);
}

void bignum_modexp(bignum_t* result, const bignum_t* base, const bignum_t* exp, const bignum_t* mod) {
    bignum_t acc;
    bignum_set_u32(&acc, 1);
    bignum_t b;
    bignum_copy(&b, base);

    int top = highest_bit_index(exp->limb, BIGNUM_LIMBS);
    int i = top;
    while (i >= 0) {
        bignum_mulmod(&acc, &acc, &acc, mod);
        if (bignum_test_bit(exp, i)) {
            bignum_mulmod(&acc, &acc, &b, mod);
        }
        i = i - 1;
    }

    bignum_copy(result, &acc);
}
