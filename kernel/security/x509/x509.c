// Minimal hand-written DER/ASN.1 reader - see x509.h's own comment for scope.

#include "x509.h"

#define ASN1_TAG_INTEGER 0x02
#define ASN1_TAG_BIT_STRING 0x03
#define ASN1_TAG_SEQUENCE 0x30
#define ASN1_TAG_CONTEXT0_CONSTRUCTED 0xA0  // [0] EXPLICIT, e.g. tbsCertificate's optional version

bool asn1_read_tlv(const u8* data, int offset, int total_len,
                    u8* tag_out, int* len_out, int* value_offset_out) {
    if (offset < 0 || offset >= total_len) {
        return false;
    }
    u8 tag = data[offset];
    int pos = offset + 1;
    if (pos >= total_len) {
        return false;
    }
    u8 first_len = data[pos];
    pos = pos + 1;

    int length;
    if ((first_len & 0x80) == 0) {
        length = (int) first_len;
    } else {
        int num_len_bytes = (int) (first_len & 0x7F);
        if (num_len_bytes == 0 || num_len_bytes > 4) {
            return false;  // indefinite-length or absurdly large - not DER, or beyond what we need
        }
        length = 0;
        int i = 0;
        while (i < num_len_bytes) {
            if (pos >= total_len) {
                return false;
            }
            length = (length << 8) | (int) data[pos];
            pos = pos + 1;
            i = i + 1;
        }
    }

    if (length < 0 || pos + length > total_len) {
        return false;
    }

    *tag_out = tag;
    *len_out = length;
    *value_offset_out = pos;
    return true;
}

// Reads one INTEGER's value into a bignum, stripping a leading 0x00
// sign-padding byte if present (DER prepends 0x00 whenever the true
// magnitude's first byte has its high bit set, to keep the value positive -
// bignum_from_bytes_be wants pure unsigned magnitude bytes).
static bool read_integer_bignum(const u8* data, int offset, int total_len, bignum_t* out) {
    u8 tag;
    int len;
    int value_offset;
    if (!asn1_read_tlv(data, offset, total_len, &tag, &len, &value_offset)) {
        return false;
    }
    if (tag != ASN1_TAG_INTEGER) {
        return false;
    }
    const u8* bytes = &data[value_offset];
    if (len > 1 && bytes[0] == 0x00) {
        bytes = &bytes[1];
        len = len - 1;
    }
    if (len > BIGNUM_LIMBS * 4) {
        return false;  // larger than this bignum's fixed capacity
    }
    bignum_from_bytes_be(out, bytes, len);
    return true;
}

// Reads one small INTEGER's value into a u32 (the RSA public exponent -
// always tiny in practice, e.g. 65537 = 3 bytes).
static bool read_integer_u32(const u8* data, int offset, int total_len, u32* out) {
    u8 tag;
    int len;
    int value_offset;
    if (!asn1_read_tlv(data, offset, total_len, &tag, &len, &value_offset)) {
        return false;
    }
    if (tag != ASN1_TAG_INTEGER) {
        return false;
    }
    const u8* bytes = &data[value_offset];
    if (len > 1 && bytes[0] == 0x00) {
        bytes = &bytes[1];
        len = len - 1;
    }
    if (len <= 0 || len > 4) {
        return false;
    }
    u32 value = 0;
    int i = 0;
    while (i < len) {
        value = (value << 8) | (u32) bytes[i];
        i = i + 1;
    }
    *out = value;
    return true;
}

bool x509_extract_rsa_pubkey(const u8* cert_der, int cert_len,
                              bignum_t* modulus_out, u32* exponent_out) {
    u8 tag;
    int len;
    int value_offset;

    // Certificate ::= SEQUENCE { tbsCertificate, signatureAlgorithm, signatureValue }
    if (!asn1_read_tlv(cert_der, 0, cert_len, &tag, &len, &value_offset) || tag != ASN1_TAG_SEQUENCE) {
        return false;
    }
    int cert_value_offset = value_offset;

    // tbsCertificate is the first child of Certificate's SEQUENCE.
    if (!asn1_read_tlv(cert_der, cert_value_offset, cert_len, &tag, &len, &value_offset) || tag != ASN1_TAG_SEQUENCE) {
        return false;
    }
    int tbs_offset = value_offset;
    int tbs_end = value_offset + len;

    // Walk tbsCertificate's children positionally (RFC 5280), skipping each
    // one generically via its own TLV length until we reach
    // subjectPublicKeyInfo - version[0](optional) serialNumber signature
    // issuer validity subject subjectPublicKeyInfo ...
    int cursor = tbs_offset;

    if (!asn1_read_tlv(cert_der, cursor, tbs_end, &tag, &len, &value_offset)) {
        return false;
    }
    if (tag == ASN1_TAG_CONTEXT0_CONSTRUCTED) {
        // optional explicit version - skip it and read the next field (serialNumber)
        cursor = value_offset + len;
        if (!asn1_read_tlv(cert_der, cursor, tbs_end, &tag, &len, &value_offset)) {
            return false;
        }
    }
    // `tag`/`len`/`value_offset` now describe serialNumber - skip it plus
    // signature/issuer/validity/subject (4 more fields) to reach
    // subjectPublicKeyInfo.
    cursor = value_offset + len;  // past serialNumber
    int fields_to_skip = 4;       // signature, issuer, validity, subject
    int i = 0;
    while (i < fields_to_skip) {
        if (!asn1_read_tlv(cert_der, cursor, tbs_end, &tag, &len, &value_offset)) {
            return false;
        }
        cursor = value_offset + len;
        i = i + 1;
    }

    // subjectPublicKeyInfo ::= SEQUENCE { algorithm AlgorithmIdentifier, subjectPublicKey BIT STRING }
    if (!asn1_read_tlv(cert_der, cursor, tbs_end, &tag, &len, &value_offset) || tag != ASN1_TAG_SEQUENCE) {
        return false;
    }
    int spki_offset = value_offset;
    int spki_end = value_offset + len;

    // algorithm (skip)
    if (!asn1_read_tlv(cert_der, spki_offset, spki_end, &tag, &len, &value_offset)) {
        return false;
    }
    int after_algorithm = value_offset + len;

    // subjectPublicKey BIT STRING
    if (!asn1_read_tlv(cert_der, after_algorithm, spki_end, &tag, &len, &value_offset) || tag != ASN1_TAG_BIT_STRING) {
        return false;
    }
    if (len < 1) {
        return false;
    }
    // First byte of a BIT STRING's value is the "unused bits" count - 0 for
    // a DER-encoded key. The rest is the inner RSAPublicKey DER encoding.
    int rsa_key_offset = value_offset + 1;
    int rsa_key_len = len - 1;
    int rsa_key_end = rsa_key_offset + rsa_key_len;

    // RSAPublicKey ::= SEQUENCE { modulus INTEGER, publicExponent INTEGER }
    if (!asn1_read_tlv(cert_der, rsa_key_offset, rsa_key_end, &tag, &len, &value_offset) || tag != ASN1_TAG_SEQUENCE) {
        return false;
    }
    int rsa_seq_offset = value_offset;
    int rsa_seq_end = value_offset + len;

    if (!read_integer_bignum(cert_der, rsa_seq_offset, rsa_seq_end, modulus_out)) {
        return false;
    }
    // advance past the modulus INTEGER to find the exponent INTEGER
    if (!asn1_read_tlv(cert_der, rsa_seq_offset, rsa_seq_end, &tag, &len, &value_offset)) {
        return false;
    }
    int exponent_field_offset = value_offset + len;
    if (!read_integer_u32(cert_der, exponent_field_offset, rsa_seq_end, exponent_out)) {
        return false;
    }

    return true;
}
