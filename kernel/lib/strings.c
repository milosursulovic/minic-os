// Hand-rolled string/number helpers (no libc). `strlen_` avoids colliding
// with gcc's builtin knowledge of the name `strlen`.

#include "strings.h"
#include "../drivers/io/io.h"

bool streq(const char* a, const char* b) {
    int i = 0;
    while (a[i] != '\0' && b[i] != '\0') {
        if (a[i] != b[i]) {
            return false;
        }
        i = i + 1;
    }
    return a[i] == b[i];
}

int strlen_(const char* s) {
    int i = 0;
    while (s[i] != '\0') {
        i = i + 1;
    }
    return i;
}

bool starts_with(const char* s, const char* prefix) {
    int i = 0;
    while (prefix[i] != '\0') {
        if (s[i] != prefix[i]) {
            return false;
        }
        i = i + 1;
    }
    return true;
}

// Accepts an optional "0x" prefix; non-hex characters are silently skipped.
u64 parse_hex(const char* s) {
    u64 value = 0;
    int i = 0;
    if (s[0] == '0' && s[1] == 'x') {
        i = 2;
    }
    while (s[i] != '\0') {
        char c = s[i];
        u64 digit = 0;
        bool valid_digit = true;
        if (c >= '0' && c <= '9') {
            digit = (u64) (c - '0');
        } else if (c >= 'a' && c <= 'f') {
            digit = (u64) (c - 'a') + 10;
        } else {
            valid_digit = false;
        }
        if (valid_digit) {
            value = value * 16 + digit;
        }
        i = i + 1;
    }
    return value;
}

void print_hex(u64 value) {
    const char* digits = "0123456789abcdef";
    char buf[17];
    buf[16] = '\0';
    if (value == 0) {
        buf[15] = digits[0];
        vga_print(&buf[15]);
        serial_print(&buf[15]);
        return;
    }
    int i = 15;
    while (value > 0 && i >= 0) {
        u64 nibble = value % 16;
        buf[i] = digits[nibble];
        value = value / 16;
        i = i - 1;
    }
    vga_print(&buf[i + 1]);
    serial_print(&buf[i + 1]);
}

// Like print_hex but writes into a caller buffer (no null terminator) and
// returns the digit count, for callers that need hex text, not output.
int format_hex(u64 value, u8* out) {
    const char* digits = "0123456789abcdef";
    char buf[16];
    if (value == 0) {
        out[0] = (u8) digits[0];
        return 1;
    }
    int i = 15;
    while (value > 0 && i >= 0) {
        u64 nibble = value % 16;
        buf[i] = digits[nibble];
        value = value / 16;
        i = i - 1;
    }
    int len = 15 - i;
    int j = 0;
    while (j < len) {
        out[j] = (u8) buf[i + 1 + j];
        j = j + 1;
    }
    return len;
}

void join_path(char* out, const char* base, const char* name) {
    int i = 0;
    while (base[i] != '\0') {
        out[i] = base[i];
        i = i + 1;
    }
    out[i] = '/';
    i = i + 1;
    int j = 0;
    while (name[j] != '\0') {
        out[i] = name[j];
        i = i + 1;
        j = j + 1;
    }
    out[i] = '\0';
}

void print_decimal(u64 value) {
    char buf[21];
    buf[20] = '\0';
    if (value == 0) {
        buf[19] = '0';
        vga_print(&buf[19]);
        serial_print(&buf[19]);
        return;
    }
    int i = 19;
    while (value > 0 && i >= 0) {
        buf[i] = (char) ('0' + (value % 10));
        value = value / 10;
        i = i - 1;
    }
    vga_print(&buf[i + 1]);
    serial_print(&buf[i + 1]);
}

bool parse_ip(const char* s, u8* out) {
    int octet = 0;
    int value = 0;
    int digits_in_octet = 0;
    int i = 0;
    while (true) {
        char c = s[i];
        if (c >= '0' && c <= '9') {
            value = value * 10 + (c - '0');
            digits_in_octet = digits_in_octet + 1;
            if (digits_in_octet > 3 || value > 255) {
                return false;
            }
        } else if (c == '.' || c == '\0') {
            if (digits_in_octet == 0 || octet >= 4) {
                return false;  // empty octet ("1..2.3") or too many dots
            }
            out[octet] = (u8) value;
            octet = octet + 1;
            value = 0;
            digits_in_octet = 0;
            if (c == '\0') {
                break;
            }
        } else {
            return false;  // anything else (letters, etc) means "not a literal IP"
        }
        i = i + 1;
    }
    return octet == 4;
}
