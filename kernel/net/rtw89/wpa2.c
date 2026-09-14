// Real, hand-written WPA2-PSK/CCMP 4-way handshake - see wpa2.h.
//
// Real EAPOL-Key wire layout (802.11i 8.5.2, big-endian multi-byte
// fields - unlike 802.11's own little-endian header, see wpa2.h's own
// comment) - fixed 99-byte portion before variable-length key data:
//   0      version (1)
//   1      type (1) - 3 = EAPOL-Key
//   2..3   length (2, BE) - real 802.1X body length after this field
//   4      descriptor_type (1) - 2 = RSN
//   5..6   key_info (2, BE)
//   7..8   key_length (2, BE)
//   9..16  replay_counter (8, BE)
//   17..48 key_nonce (32)
//   49..64 key_iv (16)
//   65..72 key_rsc (8)
//   73..80 reserved (8)
//   81..96 key_mic (16)
//   97..98 key_data_length (2, BE)
//   99..   key_data

#include "wpa2.h"
#include "../../security/hash/hmac_sha1.h"
#include "../../security/hash/pbkdf2_sha1.h"
#include "../../security/aes/aes.h"
#include "../../security/aes/aes_keywrap.h"
#include "../../lib/rand.h"

#define EAPOL_FIXED_LEN 99

// Real key_info bit positions (802.11i Table 8-8).
#define KEYINFO_DESC_VER_MASK 0x0007u
#define KEYINFO_DESC_VER_AES 2u
#define KEYINFO_PAIRWISE (1u << 3)
#define KEYINFO_INSTALL (1u << 6)
#define KEYINFO_ACK (1u << 7)
#define KEYINFO_MIC (1u << 8)
#define KEYINFO_SECURE (1u << 9)
#define KEYINFO_ENCRYPTED (1u << 12)

static void write_u16be(u8* p, u16 v) {
    p[0] = (u8) (v >> 8);
    p[1] = (u8) v;
}

static u16 read_u16be(const u8* p) {
    return (u16) (((u16) p[0] << 8) | p[1]);
}

static void write_u64be(u8* p, u64 v) {
    int i = 0;
    while (i < 8) {
        p[i] = (u8) (v >> (56 - i * 8));
        i = i + 1;
    }
}

static u64 read_u64be(const u8* p) {
    u64 v = 0;
    int i = 0;
    while (i < 8) {
        v = (v << 8) | p[i];
        i = i + 1;
    }
    return v;
}

static u32 local_strlen(const char* s) {
    u32 n = 0;
    while (s[n] != 0) {
        n = n + 1;
    }
    return n;
}

static void bytes_copy(u8* dst, const u8* src, u32 n) {
    u32 i = 0;
    while (i < n) {
        dst[i] = src[i];
        i = i + 1;
    }
}

static void bytes_zero(u8* dst, u32 n) {
    u32 i = 0;
    while (i < n) {
        dst[i] = 0;
        i = i + 1;
    }
}

static void min_max_6(const u8 a[6], const u8 b[6], const u8** lo, const u8** hi) {
    int i = 0;
    int cmp = 0;
    while (i < 6) {
        if (a[i] != b[i]) {
            cmp = (a[i] < b[i]) ? -1 : 1;
            break;
        }
        i = i + 1;
    }
    if (cmp <= 0) {
        *lo = a;
        *hi = b;
    } else {
        *lo = b;
        *hi = a;
    }
}

static void min_max_32(const u8 a[32], const u8 b[32], const u8** lo, const u8** hi) {
    int i = 0;
    int cmp = 0;
    while (i < 32) {
        if (a[i] != b[i]) {
            cmp = (a[i] < b[i]) ? -1 : 1;
            break;
        }
        i = i + 1;
    }
    if (cmp <= 0) {
        *lo = a;
        *hi = b;
    } else {
        *lo = b;
        *hi = a;
    }
}

// Real PRF-384 (802.11i 8.5.1.1): R = HMAC-SHA1(K, A || 0x00 || B || i)
// for i=0,1,..., concatenated and truncated to the requested length.
void wpa2_derive_ptk(const char* passphrase, u32 passphrase_len, const char* ssid, u32 ssid_len,
                      const u8 aa[6], const u8 spa[6], const u8 anonce[32], const u8 snonce[32],
                      u8 ptk_out[WPA2_PTK_LEN]) {
    u8 pmk[32];
    pbkdf2_sha1((const u8*) passphrase, passphrase_len, (const u8*) ssid, ssid_len, 4096, pmk, 32);

    const u8* mac_lo;
    const u8* mac_hi;
    min_max_6(aa, spa, &mac_lo, &mac_hi);
    const u8* nonce_lo;
    const u8* nonce_hi;
    min_max_32(anonce, snonce, &nonce_lo, &nonce_hi);

    // NOT `static const char*` - a static pointer initialized from a
    // string literal needs an absolute 64-bit relocation to fix up at
    // load time, unrepresentable in this kernel's ELF32 container (see
    // CLAUDE.md's own "no full 64-bit address as static data" rule).
    // A plain local computes the address via a real RIP-relative lea
    // each call instead - no relocation record needed.
    const char* label = "Pairwise key expansion";
    u32 label_len = local_strlen(label);

    u8 a_data[32 + 76];  // real max: label(23) + 0x00(1) + B(76) = 100
    bytes_copy(a_data, (const u8*) label, label_len);
    a_data[label_len] = 0;
    u32 pos = label_len + 1;
    bytes_copy(&a_data[pos], mac_lo, 6);
    bytes_copy(&a_data[pos + 6], mac_hi, 6);
    bytes_copy(&a_data[pos + 12], nonce_lo, 32);
    bytes_copy(&a_data[pos + 44], nonce_hi, 32);
    u32 a_len = pos + 76;

    u32 produced = 0;
    u8 counter = 0;
    while (produced < WPA2_PTK_LEN) {
        u8 input[100 + 1];
        bytes_copy(input, a_data, a_len);
        input[a_len] = counter;
        u8 digest[20];
        hmac_sha1(pmk, 32, input, a_len + 1, digest);
        u32 n = WPA2_PTK_LEN - produced;
        if (n > 20) {
            n = 20;
        }
        bytes_copy(&ptk_out[produced], digest, n);
        produced = produced + n;
        counter = counter + 1;
    }
}

static void compute_mic(const u8 kck[16], const u8* frame, u32 frame_len, u8 mic_out[16]) {
    u8 zeroed[EAPOL_FIXED_LEN + 256];
    u32 n = frame_len;
    if (n > sizeof(zeroed)) {
        n = sizeof(zeroed);
    }
    bytes_copy(zeroed, frame, n);
    bytes_zero(&zeroed[81], 16);
    u8 digest[20];
    hmac_sha1(kck, 16, zeroed, n, digest);
    bytes_copy(mic_out, digest, 16);
}

static void build_message(u8* out, u16 key_info, u64 replay_counter, const u8 nonce[32],
                           const u8 kck[16]) {
    bytes_zero(out, EAPOL_FIXED_LEN);
    out[0] = 2;  // 802.1X-2004
    out[1] = 3;  // EAPOL-Key
    write_u16be(&out[2], 95);  // body length after the length field, key_data_length=0
    out[4] = 2;  // RSN descriptor
    write_u16be(&out[5], key_info);
    write_u16be(&out[7], 0);  // key_length: 0 in messages 2/4
    write_u64be(&out[9], replay_counter);
    if (nonce != 0) {
        bytes_copy(&out[17], nonce, 32);
    }
    write_u16be(&out[97], 0);  // key_data_length = 0

    u8 mic[16];
    compute_mic(kck, out, EAPOL_FIXED_LEN, mic);
    bytes_copy(&out[81], mic, 16);
}

wpa2_status rtw89_wpa2_handshake(const wifi_network* net, const u8 bssid[6], const u8 our_mac[6],
                                  const char* passphrase, u32 passphrase_len, u8 tk_out[16]) {
    u8 buf[EAPOL_FIXED_LEN + 256];

    // Message 1: real ANonce + replay counter, no MIC yet (PTK unknown).
    u32 len = dot11_recv_eapol(bssid, buf, sizeof(buf), 50);  // ~500ms
    if (len < EAPOL_FIXED_LEN) {
        return WPA2_HANDSHAKE_TIMEOUT;
    }
    u16 key_info1 = read_u16be(&buf[5]);
    if ((key_info1 & KEYINFO_ACK) == 0 || (key_info1 & KEYINFO_MIC) != 0) {
        return WPA2_HANDSHAKE_TIMEOUT;  // not really message 1
    }
    u8 anonce[32];
    bytes_copy(anonce, &buf[17], 32);
    u64 replay1 = read_u64be(&buf[9]);

    u8 snonce[32];
    u32 i = 0;
    while (i < 32) {
        u32 r = rand_next();
        snonce[i] = (u8) r;
        snonce[i + 1] = (u8) (r >> 8);
        snonce[i + 2] = (u8) (r >> 16);
        snonce[i + 3] = (u8) (r >> 24);
        i = i + 4;
    }

    u8 ptk[WPA2_PTK_LEN];
    wpa2_derive_ptk(passphrase, passphrase_len, net->ssid, net->ssid_len, bssid, our_mac, anonce,
                     snonce, ptk);
    const u8* kck = &ptk[WPA2_KCK_OFFSET];
    const u8* kek = &ptk[WPA2_KEK_OFFSET];

    // Message 2: real SNonce, echoes message 1's replay counter, MIC via KCK.
    u8 msg2[EAPOL_FIXED_LEN];
    build_message(msg2, (u16) (KEYINFO_DESC_VER_AES | KEYINFO_PAIRWISE | KEYINFO_MIC), replay1,
                  snonce, kck);
    dot11_send_eapol(bssid, our_mac, msg2, EAPOL_FIXED_LEN);

    // Message 3: real ANonce (same as message 1), Install+Ack+MIC+Secure
    // set, MIC over the whole frame must verify with our own KCK - the
    // real, decisive "was the passphrase correct" check.
    len = dot11_recv_eapol(bssid, buf, sizeof(buf), 50);
    if (len < EAPOL_FIXED_LEN) {
        return WPA2_HANDSHAKE_TIMEOUT;
    }
    u16 key_info3 = read_u16be(&buf[5]);
    if ((key_info3 & KEYINFO_ACK) == 0 || (key_info3 & KEYINFO_MIC) == 0) {
        return WPA2_HANDSHAKE_TIMEOUT;
    }
    u8 received_mic[16];
    bytes_copy(received_mic, &buf[81], 16);
    u8 calc_mic[16];
    compute_mic(kck, buf, len, calc_mic);
    i = 0;
    while (i < 16) {
        if (received_mic[i] != calc_mic[i]) {
            return WPA2_HANDSHAKE_MIC_FAIL;
        }
        i = i + 1;
    }
    u64 replay3 = read_u64be(&buf[9]);

    // Real GTK is present in message 3's key data, AES-KW-wrapped
    // (EncryptedKeyData bit) - unwrapped here for real protocol
    // completeness, but this driver has no broadcast/multicast RX
    // path (a real, stated scope limit - see the plan file), so the
    // recovered GTK bytes are intentionally not installed anywhere.
    u16 key_data_len3 = read_u16be(&buf[97]);
    if ((key_info3 & KEYINFO_ENCRYPTED) != 0 && key_data_len3 >= 16 && key_data_len3 % 8 == 0
        && (u32) EAPOL_FIXED_LEN + key_data_len3 <= len) {
        u8 kek_round_keys[AES128_ROUND_KEY_BYTES];
        aes128_key_expand(kek, kek_round_keys);
        static u8 unwrapped_gtk[256];
        aes_key_unwrap(kek_round_keys, &buf[EAPOL_FIXED_LEN], key_data_len3, unwrapped_gtk);
    }

    // Message 4: real ack, MIC only, echoes message 3's replay counter.
    u8 msg4[EAPOL_FIXED_LEN];
    build_message(msg4, (u16) (KEYINFO_DESC_VER_AES | KEYINFO_PAIRWISE | KEYINFO_MIC | KEYINFO_SECURE),
                  replay3, 0, kck);
    dot11_send_eapol(bssid, our_mac, msg4, EAPOL_FIXED_LEN);

    bytes_copy(tk_out, &ptk[WPA2_TK_OFFSET], 16);
    return WPA2_HANDSHAKE_OK;
}
