#pragma once
#include "../../../types.h"
#include "../bignum/bignum.h"

#pragma GCC visibility push(hidden)

// Minimal, hand-written DER/ASN.1 reader - enough to pull an RSA public key
// (modulus + exponent) out of a real X.509 certificate's own DER encoding.
// Deliberately NOT a general ASN.1 library: no OID table, no signature
// verification, no certificate chain walking - see kernel/net/tls/tls.c's
// own comment for why (trust-on-first-use, a stated scope limit).

// Reads one DER tag-length-value header starting at `offset` within
// `data[0..total_len)`. On success, `*tag_out` is the raw tag byte,
// `*len_out` is the value's length, and `*value_offset_out` is where the
// value bytes start (the value itself is `data[*value_offset_out .. +*len_out)`).
// Returns false on any malformed/truncated encoding.
bool asn1_read_tlv(const u8* data, int offset, int total_len,
                    u8* tag_out, int* len_out, int* value_offset_out);

// Extracts the RSA public key (modulus, exponent) from a DER-encoded X.509
// certificate (as received in a TLS Certificate handshake message).
// Returns false if the certificate isn't a well-formed RSA certificate this
// minimal parser understands.
bool x509_extract_rsa_pubkey(const u8* cert_der, int cert_len,
                              bignum_t* modulus_out, u32* exponent_out);

#pragma GCC visibility pop
