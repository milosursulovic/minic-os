#include "memory.h"
#include "../../../kernel/drivers/io/io.h"
#include "../../../kernel/drivers/keyboard/keyboard.h"
#include "../../../kernel/lib/strings.h"
#include "../../../kernel/mm/heap/heap.h"
#include "../../../kernel/mm/frames/frames.h"
#include "../../../kernel/mm/paging/paging.h"

void cmd_alloc(void) {
    void* p = kalloc(64);
    if (p == NULL) {
        vga_print("alloc failed - heap full");
        serial_print("alloc failed - heap full");
    } else {
        g_last_alloc = p;
        vga_print("allocated 64 bytes at 0x");
        serial_print("allocated 64 bytes at 0x");
        print_hex((u64) p);
    }
}

void cmd_big_alloc(void) {
    void* p = kalloc(65536);  // bigger than one heap_grow() chunk
    if (p == NULL) {
        vga_print("bigalloc failed - heap full");
        serial_print("bigalloc failed - heap full");
    } else {
        g_last_alloc = p;
        vga_print("allocated 65536 bytes at 0x");
        serial_print("allocated 65536 bytes at 0x");
        print_hex((u64) p);
    }
}

void cmd_free(void) {
    if (g_last_alloc == NULL) {
        vga_print("nothing to free");
        serial_print("nothing to free");
        return;
    }
    vga_print("freed 0x");
    serial_print("freed 0x");
    print_hex((u64) g_last_alloc);
    kfree(g_last_alloc);
    g_last_alloc = NULL;
}

void cmd_free_addr(void) {
    u64 addr = parse_hex(&g_line_buffer[5]);  // past "free "
    kfree((void*) addr);
    vga_print("freed 0x");
    serial_print("freed 0x");
    print_hex(addr);
}

void cmd_mem(void) {
    vga_print("free: 0x");
    serial_print("free: 0x");
    print_hex(heap_free_bytes());
    vga_print(" / 0x");
    serial_print(" / 0x");
    print_hex(g_heap_size);
}

void cmd_frames(void) {
    vga_print("free frames: 0x");
    serial_print("free frames: 0x");
    print_hex((u64) g_free_frame_count);
    vga_putc(' ');
    serial_putc(' ');
    vga_print("/ 0x");
    serial_print("/ 0x");
    print_hex((u64) g_total_frames);
}

void cmd_frame(void) {
    void* f = alloc_frame();
    if (f == NULL) {
        vga_print("out of frames");
        serial_print("out of frames");
    } else {
        g_last_frame = f;
        vga_print("allocated frame at 0x");
        serial_print("allocated frame at 0x");
        print_hex((u64) f);
    }
}

void cmd_unframe(void) {
    if (g_last_frame == NULL) {
        vga_print("nothing to unframe");
        serial_print("nothing to unframe");
        return;
    }
    vga_print("freed frame 0x");
    serial_print("freed frame 0x");
    print_hex((u64) g_last_frame);
    free_frame(g_last_frame);
    g_last_frame = NULL;
}

void cmd_map(void) {
    void* frame = alloc_frame();
    if (frame == NULL) {
        vga_print("out of frames");
        serial_print("out of frames");
        return;
    }
    u64 vaddr = 0x40000000;  // 1GB - just past boot.s's static identity map
    bool ok = map_page(vaddr, (u64) frame, 0x02 | PAGE_NX);  // writable, non-executable
    if (!ok) {
        vga_print("map failed");
        serial_print("map failed");
        free_frame(frame);
        return;
    }

    u32* p = (u32*) vaddr;
    p[0] = 0xCAFEBABE;
    u32 read_back = p[0];

    vga_print("mapped 0x");
    serial_print("mapped 0x");
    print_hex(vaddr);
    vga_print(" -> 0x");
    serial_print(" -> 0x");
    print_hex((u64) frame);
    vga_print(", wrote/read 0x");
    serial_print(", wrote/read 0x");
    print_hex((u64) read_back);
}
