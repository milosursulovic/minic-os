// Host-side signing tool for minic-os executables (Faza I point 14, item
// 17) - a plain gcc/libc program, NOT built through the freestanding
// kernel pipeline. Signs a raw flat .bin (objcopy -O binary output) with
// the same hand-written HMAC-SHA256 (kernel/security/hash/) and the same
// embedded key (kernel/security/exec_sign/signing_key.h) the kernel
// itself verifies against at spawn time - this file #includes those
// exact kernel sources directly, so signer and verifier are always the
// same code, never a hand-duplicated copy that could drift.
//
// Usage: sign_exec <input.bin> <output.bin>

#include <stdio.h>
#include <stdlib.h>
#include "../kernel/security/exec_sign/exec_sign.h"

int main(int argc, char** argv) {
    if (argc != 3) {
        fprintf(stderr, "usage: %s <input.bin> <output.bin>\n", argv[0]);
        return 1;
    }

    FILE* in = fopen(argv[1], "rb");
    if (in == NULL) {
        fprintf(stderr, "sign_exec: cannot open %s\n", argv[1]);
        return 1;
    }
    fseek(in, 0, SEEK_END);
    long size = ftell(in);
    fseek(in, 0, SEEK_SET);
    if (size < 0) {
        fprintf(stderr, "sign_exec: cannot determine size of %s\n", argv[1]);
        fclose(in);
        return 1;
    }
    u8* payload = (u8*) malloc((size_t) size);
    if (payload == NULL) {
        fprintf(stderr, "sign_exec: out of memory\n");
        fclose(in);
        return 1;
    }
    if (fread(payload, 1, (size_t) size, in) != (size_t) size) {
        fprintf(stderr, "sign_exec: short read on %s\n", argv[1]);
        fclose(in);
        free(payload);
        return 1;
    }
    fclose(in);

    exec_sign_header header;
    exec_sign_produce(payload, (u32) size, &header);

    FILE* out = fopen(argv[2], "wb");
    if (out == NULL) {
        fprintf(stderr, "sign_exec: cannot open %s for writing\n", argv[2]);
        free(payload);
        return 1;
    }
    fwrite(&header, sizeof(header), 1, out);
    fwrite(payload, 1, (size_t) size, out);
    fclose(out);
    free(payload);

    printf("sign_exec: wrote %s (%ld byte payload + %zu byte header)\n", argv[2], size, sizeof(header));
    return 0;
}
