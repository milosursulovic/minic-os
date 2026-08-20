// Interactive shell over keyboard.c's line buffer.

#include "shell.h"
#include "../drivers/io.h"
#include "../drivers/keyboard.h"
#include "../lib/strings.h"
#include "../mm/heap.h"
#include "../mm/frames.h"
#include "../mm/paging.h"
#include "../sched/task.h"
#include "../proc/channel.h"
#include "../isr/isr.h"
#include "../disk/ata.h"
#include "../disk/minifs.h"
#include "../disk/vfs.h"
#include "../proc/process.h"
#include "../proc/object.h"
#include "../drivers/pci.h"
#include "../net/e1000.h"
#include "../net/arp.h"
#include "../net/ip.h"
#include "../net/icmp.h"
#include "../net/dns.h"
#include "../net/tcp.h"
#include "../drivers/vbe.h"

#pragma GCC visibility push(hidden)
extern u8 g_test_prog_start;
extern u8 g_test_prog_end;
#pragma GCC visibility pop

void print_prompt(void) {
    vga_print("> ");
    serial_print("> ");
}

static void cmd_help(void) {
    vga_print("commands: help clear ticks alloc bigalloc free free <addr> mem reset frame unframe frames map tasks procs ps objs netconns chan send disk diskwrite mkfs mkfile cat ls vfscat <path> vfswrite install spawn ring3go ring3fault ring3nx ring3reg ring3unreg ring3async ring3asyncwrite ring3asyncping ring3asyncdns ring3asynctcp pci nic fb arp ping dns tcp echo <text>");
    serial_print("commands: help clear ticks alloc bigalloc free free <addr> mem reset frame unframe frames map tasks procs ps objs netconns chan send disk diskwrite mkfs mkfile cat ls vfscat <path> vfswrite install spawn ring3go ring3fault ring3nx ring3reg ring3unreg ring3async ring3asyncwrite ring3asyncping ring3asyncdns ring3asynctcp pci nic fb arp ping dns tcp echo <text>");
}

static void cmd_ticks(void) {
    vga_print("ticks: 0x");
    serial_print("ticks: 0x");
    print_hex(g_tick_count);
}

static void cmd_alloc(void) {
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

static void cmd_big_alloc(void) {
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

static void cmd_free(void) {
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

static void cmd_free_addr(void) {
    u64 addr = parse_hex(&g_line_buffer[5]);  // past "free "
    kfree((void*) addr);
    vga_print("freed 0x");
    serial_print("freed 0x");
    print_hex(addr);
}

static void cmd_mem(void) {
    vga_print("free: 0x");
    serial_print("free: 0x");
    print_hex(heap_free_bytes());
    vga_print(" / 0x");
    serial_print(" / 0x");
    print_hex(g_heap_size);
}

static void cmd_reset(void) {
    heap_init();
    g_last_alloc = NULL;
    vga_print("heap reset");
    serial_print("heap reset");
}

static void cmd_frames(void) {
    vga_print("free frames: 0x");
    serial_print("free frames: 0x");
    print_hex((u64) g_free_frame_count);
    vga_putc(' ');
    serial_putc(' ');
    vga_print("/ 0x");
    serial_print("/ 0x");
    print_hex((u64) g_total_frames);
}

static void cmd_frame(void) {
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

static void cmd_unframe(void) {
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

static void cmd_map(void) {
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

static void cmd_tasks(void) {
    vga_print("task1: 0x");
    serial_print("task1: 0x");
    print_hex(g_task1_ticks);
    vga_print(" task2: 0x");
    serial_print(" task2: 0x");
    print_hex(g_task2_ticks);
    vga_print(" task3: 0x");
    serial_print(" task3: 0x");
    print_hex(g_task3_ticks);
    vga_print(" task4: 0x");
    serial_print(" task4: 0x");
    print_hex(g_task4_ticks);
    vga_print(" ticks: 0x");
    serial_print(" ticks: 0x");
    print_hex(g_tick_count);
}

static void cmd_procs(void) {
    vga_print("proc_a: 0x");
    serial_print("proc_a: 0x");
    print_hex((u64) g_proc_a_value);
    vga_print(" @phys 0x");
    serial_print(" @phys 0x");
    print_hex(g_proc_a_phys);
    vga_print(" proc_b: 0x");
    serial_print(" proc_b: 0x");
    print_hex((u64) g_proc_b_value);
    vga_print(" @phys 0x");
    serial_print(" @phys 0x");
    print_hex(g_proc_b_phys);
}

static void cmd_chan(void) {
    vga_print("receiver got: 0x");
    serial_print("receiver got: 0x");
    print_hex((u64) g_receiver_got_message);
    vga_print(" value=0x");
    serial_print(" value=0x");
    print_hex(g_receiver_value);
}

static void cmd_send(void) {
    bool ok = channel_send(g_channel_demo, 0xC0FFEE1234);
    if (!ok) {
        vga_print("send failed - channel full");
        serial_print("send failed - channel full");
        return;
    }
    vga_print("sent 0xc0ffee1234");
    serial_print("sent 0xc0ffee1234");
}

static void cmd_clear(void) {
    int i = 80;  // leave the boot message on row 0
    while (i < 2000) {
        g_vga[i].character = ' ';
        g_vga[i].color = 0x07;
        i = i + 1;
    }
    g_vga_cursor = 80;
}

static void cmd_ps(void) {
    vga_print("processes: 0x");
    serial_print("processes: 0x");
    print_hex((u64) g_process_count);
    int i = 0;
    while (i < g_process_count) {
        vga_print(" proc");
        serial_print(" proc");
        print_hex((u64) i);
        vga_print(" task=0x");
        serial_print(" task=0x");
        print_hex((u64) g_processes[i].task_index);
        vga_print(" cr3=0x");
        serial_print(" cr3=0x");
        print_hex(g_processes[i].cr3);
        vga_print(" exited=0x");
        serial_print(" exited=0x");
        print_hex((u64) !g_processes[i].used);
        i = i + 1;
    }
}

static void cmd_objs(void) {
    vga_print("objects: 0x");
    serial_print("objects: 0x");
    print_hex((u64) g_object_count);
    if (g_object_count > 0) {
        vga_print(" obj0 type=0x");
        serial_print(" obj0 type=0x");
        print_hex((u64) g_objects[0].type);
        vga_print(" data_index=0x");
        serial_print(" data_index=0x");
        print_hex((u64) g_objects[0].data_index);
    }
}

static void cmd_netconns(void) {
    vga_print("tcp connections:");
    serial_print("tcp connections:");
    int i = 0;
    while (i < TCP_CONNECTION_SLOTS) {
        if (g_tcp_connections[i].used) {
            vga_print(" conn");
            serial_print(" conn");
            print_hex((u64) i);
            vga_print(" local_port=0x");
            serial_print(" local_port=0x");
            print_hex((u64) g_tcp_connections[i].local_port);
            vga_print(" remote=0x");
            serial_print(" remote=0x");
            print_hex((u64) g_tcp_connections[i].remote_ip[0]);
            vga_print(".0x");
            serial_print(".0x");
            print_hex((u64) g_tcp_connections[i].remote_ip[1]);
            vga_print(".0x");
            serial_print(".0x");
            print_hex((u64) g_tcp_connections[i].remote_ip[2]);
            vga_print(".0x");
            serial_print(".0x");
            print_hex((u64) g_tcp_connections[i].remote_ip[3]);
            vga_print(":0x");
            serial_print(":0x");
            print_hex((u64) g_tcp_connections[i].remote_port);
        }
        i = i + 1;
    }
}

static void cmd_disk(void) {
    u8 buf[512];
    bool ok = ata_read_sector(1, buf);
    if (!ok) {
        vga_print("disk read failed");
        serial_print("disk read failed");
        return;
    }
    char* s = (char*) &buf[0];
    vga_print("sector 1: ");
    serial_print("sector 1: ");
    vga_print(s);
    serial_print(s);
}

static void cmd_disk_write(void) {
    u8 write_buf[512];
    int i = 0;
    while (i < 512) {
        write_buf[i] = (u8) (i & 0xFF);
        i = i + 1;
    }
    bool wrote = ata_write_sector(100, write_buf);
    if (!wrote) {
        vga_print("disk write failed");
        serial_print("disk write failed");
        return;
    }
    u8 read_buf[512];
    bool read_ok = ata_read_sector(100, read_buf);
    if (!read_ok) {
        vga_print("disk write ok, readback failed");
        serial_print("disk write ok, readback failed");
        return;
    }
    bool match = true;
    i = 0;
    while (i < 512) {
        if (write_buf[i] != read_buf[i]) {
            match = false;
        }
        i = i + 1;
    }
    if (match) {
        vga_print("write+readback verified, 512/512 bytes match");
        serial_print("write+readback verified, 512/512 bytes match");
    } else {
        vga_print("MISMATCH - write or read is broken");
        serial_print("MISMATCH - write or read is broken");
    }
}

static void cmd_mkfs(void) {
    bool ok = mkfs();
    if (ok) {
        vga_print("filesystem formatted");
        serial_print("filesystem formatted");
    } else {
        vga_print("mkfs failed");
        serial_print("mkfs failed");
    }
}

static int g_next_file_index;
static char g_last_file_name[20];

// Creates a new file each call: file0.mfs, file1.mfs, ...
static void cmd_mkfile(void) {
    char name_buf[20];
    name_buf[0] = 'f'; name_buf[1] = 'i'; name_buf[2] = 'l'; name_buf[3] = 'e';
    name_buf[4] = (char) ('0' + (u8) (g_next_file_index % 10));
    name_buf[5] = '.'; name_buf[6] = 'm'; name_buf[7] = 'f'; name_buf[8] = 's';
    name_buf[9] = '\0';

    char content_buf[64];
    const char* prefix = "Hello from MiniFS, this is file #";
    int i = 0;
    while (prefix[i] != '\0') {
        content_buf[i] = prefix[i];
        i = i + 1;
    }
    content_buf[i] = (char) ('0' + (u8) (g_next_file_index % 10));
    i = i + 1;
    content_buf[i] = '\0';
    i = i + 1;

    bool ok = fs_write_file(name_buf, (u8*) &content_buf[0], (u32) i);
    if (!ok) {
        vga_print("mkfile failed");
        serial_print("mkfile failed");
        return;
    }
    copy_name(&g_last_file_name[0], name_buf);
    g_next_file_index = g_next_file_index + 1;
    vga_print("created ");
    serial_print("created ");
    vga_print(name_buf);
    serial_print(name_buf);
}

static void cmd_cat(void) {
    if (g_last_file_name[0] == '\0') {
        vga_print("no file yet - run mkfile first");
        serial_print("no file yet - run mkfile first");
        return;
    }
    u8 buf[65];
    int n = fs_read_file(&g_last_file_name[0], buf, 64);
    if (n < 0) {
        vga_print("cat failed");
        serial_print("cat failed");
        return;
    }
    buf[n] = 0;
    char* s = (char*) &buf[0];
    vga_print(s);
    serial_print(s);
}

static void cmd_ls(void) {
    u32 file_count;
    if (!fs_superblock_info(&file_count)) {
        vga_print("ls failed - disk read error");
        serial_print("ls failed - disk read error");
        return;
    }
    vga_print("file_count: 0x");
    serial_print("file_count: 0x");
    print_hex((u64) file_count);
    vga_print("  ");
    serial_print("  ");

    int i = 0;
    int shown = 0;
    while (i < MINIFS_MAX_FILES) {
        char name[20];
        u32 size;
        if (fs_list_entry(i, name, &size)) {
            vga_print(name);
            serial_print(name);
            vga_print(" 0x");
            serial_print(" 0x");
            print_hex((u64) size);
            vga_print("  ");
            serial_print("  ");
            shown = shown + 1;
        }
        i = i + 1;
    }
    if (shown == 0) {
        vga_print("(empty)");
        serial_print("(empty)");
    }
}

static void cmd_vfs_cat(void) {
    char* path = &g_line_buffer[7];  // past "vfscat "
    u8 buf[256];
    int n = vfs_read(path, buf, 256);
    if (n == -2) {
        vga_print("vfscat: file too large to display");
        serial_print("vfscat: file too large to display");
        return;
    }
    if (n < 0) {
        vga_print("vfscat: not found");
        serial_print("vfscat: not found");
        return;
    }
    buf[n] = 0;
    char* s = (char*) &buf[0];
    vga_print(s);
    serial_print(s);
}

static void cmd_vfs_write(void) {
    const char* content = "This file was written through the VFS layer, not MiniFS directly.";
    int len = strlen_(content) + 1;  // include the null terminator, same as mkfile's content
    bool ok = vfs_write("/system/vfsdemo.mfs", (u8*) content, (u32) len);
    if (ok) {
        vga_print("wrote /system/vfsdemo.mfs via VFS");
        serial_print("wrote /system/vfsdemo.mfs via VFS");
    } else {
        vga_print("vfswrite failed");
        serial_print("vfswrite failed");
    }
}

static void cmd_install(void) {
    u32 len = (u32) ((u64) &g_test_prog_end - (u64) &g_test_prog_start);
    bool ok = vfs_write("/system/testprog.bin", &g_test_prog_start, len);
    if (!ok) {
        vga_print("install failed");
        serial_print("install failed");
        return;
    }
    vga_print("installed /system/testprog.bin, 0x");
    serial_print("installed /system/testprog.bin, 0x");
    print_hex((u64) len);
    vga_print(" bytes");
    serial_print(" bytes");
}

static void cmd_spawn(void) {
    int idx = spawn_process_from_path("/system/testprog.bin", 0x80000000, 0x80020000);
    if (idx < 0) {
        vga_print("spawn failed");
        serial_print("spawn failed");
        return;
    }
    vga_print("spawned process 0x");
    serial_print("spawned process 0x");
    print_hex((u64) idx);
}

// Wakes the boot-time ring3 process's blocked Channel.receive() -
// run `install` first so the file it spawns exists on disk.
static void cmd_ring3_go(void) {
    bool ok = channel_send(g_ring3_channel_demo, 0x1);
    if (!ok) {
        vga_print("ring3go failed - channel full");
        serial_print("ring3go failed - channel full");
        return;
    }
    vga_print("sent ring3 spawn trigger");
    serial_print("sent ring3 spawn trigger");
}

// Triggers a deliberate forbidden write - KERNEL-HALTING, run standalone.
static void cmd_ring3_fault(void) {
    bool ok = channel_send(g_ring3_channel_demo, 0x2);
    if (!ok) {
        vga_print("ring3fault failed - channel full");
        serial_print("ring3fault failed - channel full");
        return;
    }
    vga_print("sent ring3 forbidden-write trigger - expect a page fault");
    serial_print("sent ring3 forbidden-write trigger - expect a page fault");
}

// Triggers a stack-execution attempt - KERNEL-HALTING, run standalone.
static void cmd_ring3_nx(void) {
    bool ok = channel_send(g_ring3_channel_demo, 0x3);
    if (!ok) {
        vga_print("ring3nx failed - channel full");
        serial_print("ring3nx failed - channel full");
        return;
    }
    vga_print("sent ring3 stack-execution trigger - expect a page fault");
    serial_print("sent ring3 stack-execution trigger - expect a page fault");
}

// Registers testprog.bin at runtime and spawns it by index - run `install` first.
static void cmd_ring3_register(void) {
    bool ok = channel_send(g_ring3_channel_demo, 0x4);
    if (!ok) {
        vga_print("ring3reg failed - channel full");
        serial_print("ring3reg failed - channel full");
        return;
    }
    vga_print("sent ring3 register-service trigger");
    serial_print("sent ring3 register-service trigger");
}

// Registers, unregisters, then re-registers to confirm slot reuse - run `install` first.
static void cmd_ring3_unregister(void) {
    bool ok = channel_send(g_ring3_channel_demo, 0x5);
    if (!ok) {
        vga_print("ring3unreg failed - channel full");
        serial_print("ring3unreg failed - channel full");
        return;
    }
    vga_print("sent ring3 unregister-service trigger");
    serial_print("sent ring3 unregister-service trigger");
}

// Issues an async read, does work, then collects the result.
static void cmd_ring3_async(void) {
    bool ok = channel_send(g_ring3_channel_demo, 0x6);
    if (!ok) {
        vga_print("ring3async failed - channel full");
        serial_print("ring3async failed - channel full");
        return;
    }
    vga_print("sent ring3 async-read trigger");
    serial_print("sent ring3 async-read trigger");
}

// Issues an async write, then verifies it via a sync read.
static void cmd_ring3_async_write(void) {
    bool ok = channel_send(g_ring3_channel_demo, 0x7);
    if (!ok) {
        vga_print("ring3asyncwrite failed - channel full");
        serial_print("ring3asyncwrite failed - channel full");
        return;
    }
    vga_print("sent ring3 async-write trigger");
    serial_print("sent ring3 async-write trigger");
}

// Issues an async ICMP ping to the gateway.
static void cmd_ring3_async_ping(void) {
    bool ok = channel_send(g_ring3_channel_demo, 0x8);
    if (!ok) {
        vga_print("ring3asyncping failed - channel full");
        serial_print("ring3asyncping failed - channel full");
        return;
    }
    vga_print("sent ring3 async-ping trigger");
    serial_print("sent ring3 async-ping trigger");
}

// Issues an async DNS resolve for example.com.
static void cmd_ring3_async_dns(void) {
    bool ok = channel_send(g_ring3_channel_demo, 0x9);
    if (!ok) {
        vga_print("ring3asyncdns failed - channel full");
        serial_print("ring3asyncdns failed - channel full");
        return;
    }
    vga_print("sent ring3 async-dns trigger");
    serial_print("sent ring3 async-dns trigger");
}

// Chains an async DNS resolve into an async TCP fetch of example.com.
static void cmd_ring3_async_tcp(void) {
    bool ok = channel_send(g_ring3_channel_demo, 0xA);
    if (!ok) {
        vga_print("ring3asynctcp failed - channel full");
        serial_print("ring3asynctcp failed - channel full");
        return;
    }
    vga_print("sent ring3 async-tcp trigger");
    serial_print("sent ring3 async-tcp trigger");
}

static void print_mac(u8* mac) {
    int i = 0;
    while (i < 6) {
        print_hex((u64) mac[i]);
        if (i < 5) {
            vga_print(":");
            serial_print(":");
        }
        i = i + 1;
    }
}

static void cmd_pci(void) {
    pci_enumerate();
    vga_print("pci devices: 0x");
    serial_print("pci devices: 0x");
    print_hex((u64) g_pci_device_count);
    int i = 0;
    while (i < g_pci_device_count) {
        pci_device* d = &g_pci_devices[i];
        vga_print(" ");
        serial_print(" ");
        print_hex((u64) d->bus);
        vga_print(":");
        serial_print(":");
        print_hex((u64) d->device);
        vga_print(".");
        serial_print(".");
        print_hex((u64) d->function);
        vga_print(" vendor=0x");
        serial_print(" vendor=0x");
        print_hex((u64) d->vendor_id);
        vga_print(" device=0x");
        serial_print(" device=0x");
        print_hex((u64) d->device_id);
        vga_print(" class=0x");
        serial_print(" class=0x");
        print_hex((u64) d->class_code);
        vga_print(" subclass=0x");
        serial_print(" subclass=0x");
        print_hex((u64) d->subclass);
        i = i + 1;
    }
}

static void cmd_nic(void) {
    bool ok = e1000_init();
    if (!ok) {
        vga_print("e1000 init failed - device not found at 0:3.0");
        serial_print("e1000 init failed - device not found at 0:3.0");
        return;
    }
    u8 mac[6];
    e1000_get_mac(&mac[0]);
    vga_print("e1000 mac=");
    serial_print("e1000 mac=");
    print_mac(&mac[0]);
    bool link_up = e1000_link_up();
    vga_print(" link_up=0x");
    serial_print(" link_up=0x");
    print_hex((u64) link_up);
}

// Sets an 800x600x32 linear framebuffer, draws a background fill plus a
// contrasting rect, then reads pixels back (not just the values we sent) to
// prove real hardware round trips, including exact rect-boundary precision.
static void cmd_fb(void) {
    bool ok = vbe_init(800, 600);
    if (!ok) {
        vga_print("framebuffer init failed - no Bochs VBE VGA device found");
        serial_print("framebuffer init failed - no Bochs VBE VGA device found");
        return;
    }

    vga_print("fb lfb_phys=0x");
    serial_print("fb lfb_phys=0x");
    print_hex((u64) vbe_lfb_phys());
    vga_print(" vaddr=0x");
    serial_print(" vaddr=0x");
    print_hex(g_fb_vaddr);
    vga_print(" xres=0x");
    serial_print(" xres=0x");
    print_hex((u64) vbe_read_reg(1));
    vga_print(" yres=0x");
    serial_print(" yres=0x");
    print_hex((u64) vbe_read_reg(2));
    vga_print(" bpp=0x");
    serial_print(" bpp=0x");
    print_hex((u64) vbe_read_reg(3));
    vga_print(" pitch=0x");
    serial_print(" pitch=0x");
    print_hex((u64) g_fb_pitch);

    fb_fill_rect(0, 0, 800, 600, 0x00001133);
    fb_fill_rect(100, 100, 200, 150, 0x00FF0000);

    vga_print(" bg=0x");
    serial_print(" bg=0x");
    print_hex((u64) fb_get_pixel(0, 0));
    vga_print(" just_outside=0x");
    serial_print(" just_outside=0x");
    print_hex((u64) fb_get_pixel(99, 100));
    vga_print(" rect_topleft=0x");
    serial_print(" rect_topleft=0x");
    print_hex((u64) fb_get_pixel(100, 100));
    vga_print(" rect_bottomright=0x");
    serial_print(" rect_bottomright=0x");
    print_hex((u64) fb_get_pixel(299, 249));
    vga_print(" past_rect=0x");
    serial_print(" past_rect=0x");
    print_hex((u64) fb_get_pixel(300, 250));
}

// Resolves the gateway, resolves it again (cache hit), resolves the DNS
// proxy, and resolves an unreachable address (must fail cleanly).
static void cmd_arp(void) {
    u8 gateway_ip[4];
    gateway_ip[0] = 10;
    gateway_ip[1] = 0;
    gateway_ip[2] = 2;
    gateway_ip[3] = 2;

    u8 mac[6];
    u64 t0 = g_tick_count;
    bool ok1 = arp_resolve(&gateway_ip[0], &mac[0]);
    u64 elapsed1 = g_tick_count - t0;
    vga_print("resolve gateway ok=0x");
    serial_print("resolve gateway ok=0x");
    print_hex((u64) ok1);
    if (ok1) {
        vga_print(" mac=");
        serial_print(" mac=");
        print_mac(&mac[0]);
    }
    vga_print(" elapsed_ticks=0x");
    serial_print(" elapsed_ticks=0x");
    print_hex(elapsed1);

    u8 mac2[6];
    u64 t1 = g_tick_count;
    bool ok2 = arp_resolve(&gateway_ip[0], &mac2[0]);
    u64 elapsed2 = g_tick_count - t1;
    vga_print(" cached_ok=0x");
    serial_print(" cached_ok=0x");
    print_hex((u64) ok2);
    vga_print(" cached_elapsed_ticks=0x");
    serial_print(" cached_elapsed_ticks=0x");
    print_hex(elapsed2);

    u8 dns_ip[4];
    dns_ip[0] = 10;
    dns_ip[1] = 0;
    dns_ip[2] = 2;
    dns_ip[3] = 3;
    u8 mac3[6];
    bool ok3 = arp_resolve(&dns_ip[0], &mac3[0]);
    vga_print(" resolve_dns_proxy_ok=0x");
    serial_print(" resolve_dns_proxy_ok=0x");
    print_hex((u64) ok3);
    if (ok3) {
        vga_print(" dns_mac=");
        serial_print(" dns_mac=");
        print_mac(&mac3[0]);
    }

    u8 unreachable_ip[4];
    unreachable_ip[0] = 10;
    unreachable_ip[1] = 0;
    unreachable_ip[2] = 2;
    unreachable_ip[3] = 99;
    u8 mac4[6];
    bool ok4 = arp_resolve(&unreachable_ip[0], &mac4[0]);
    vga_print(" resolve_unreachable_ok=0x");
    serial_print(" resolve_unreachable_ok=0x");
    print_hex((u64) ok4);
}

static void cmd_ping(void) {
    u8 gateway_ip[4];
    gateway_ip[0] = 10;
    gateway_ip[1] = 0;
    gateway_ip[2] = 2;
    gateway_ip[3] = 2;

    u64 start_tick = g_tick_count;
    bool ok = icmp_ping(&gateway_ip[0], 0x1234, 0x1);
    u64 elapsed = g_tick_count - start_tick;

    vga_print("ping gateway ok=0x");
    serial_print("ping gateway ok=0x");
    print_hex((u64) ok);
    vga_print(" elapsed_ticks=0x");
    serial_print(" elapsed_ticks=0x");
    print_hex(elapsed);
}

static void cmd_dns(void) {
    u64 start_tick = g_tick_count;
    bool ok = dns_query("example.com");
    u64 elapsed = g_tick_count - start_tick;

    vga_print("dns query ok=0x");
    serial_print("dns query ok=0x");
    print_hex((u64) ok);
    vga_print(" elapsed_ticks=0x");
    serial_print(" elapsed_ticks=0x");
    print_hex(elapsed);
}

static void cmd_tcp(void) {
    u8 ip[4];
    if (!dns_resolve_a("example.com", &ip[0])) {
        vga_print("tcp: could not resolve example.com");
        serial_print("tcp: could not resolve example.com");
        return;
    }
    vga_print("resolved example.com -> 0x");
    serial_print("resolved example.com -> 0x");
    print_hex(ip[0]);
    vga_print(".0x");
    serial_print(".0x");
    print_hex(ip[1]);
    vga_print(".0x");
    serial_print(".0x");
    print_hex(ip[2]);
    vga_print(".0x");
    serial_print(".0x");
    print_hex(ip[3]);

    const char* request = "GET / HTTP/1.1\r\nHost: example.com\r\nConnection: close\r\n\r\n";
    u8 response[512];
    u32 response_len = 0;
    u64 start_tick = g_tick_count;
    bool ok = tcp_fetch(&ip[0], 80, request, (u16) strlen_(request), &response[0], 512, &response_len);
    u64 elapsed = g_tick_count - start_tick;

    vga_print(" tcp_fetch_ok=0x");
    serial_print(" tcp_fetch_ok=0x");
    print_hex((u64) ok);
    vga_print(" response_len=0x");
    serial_print(" response_len=0x");
    print_hex((u64) response_len);
    vga_print(" elapsed_ticks=0x");
    serial_print(" elapsed_ticks=0x");
    print_hex(elapsed);

    bool got_http_status = response_len >= 4
        && response[0] == 'H' && response[1] == 'T' && response[2] == 'T' && response[3] == 'P';
    vga_print(" got_http_status=0x");
    serial_print(" got_http_status=0x");
    print_hex((u64) got_http_status);
}

static void cmd_echo(void) {
    char* text = &g_line_buffer[5];  // past "echo "
    vga_print(text);
    serial_print(text);
}

void run_command(void) {
    if (streq(g_line_buffer, "help")) {
        cmd_help();
    } else if (streq(g_line_buffer, "clear")) {
        cmd_clear();
    } else if (streq(g_line_buffer, "ticks")) {
        cmd_ticks();
    } else if (streq(g_line_buffer, "alloc")) {
        cmd_alloc();
    } else if (streq(g_line_buffer, "bigalloc")) {
        cmd_big_alloc();
    } else if (streq(g_line_buffer, "free")) {
        cmd_free();
    } else if (starts_with(g_line_buffer, "free ")) {
        cmd_free_addr();
    } else if (streq(g_line_buffer, "mem")) {
        cmd_mem();
    } else if (streq(g_line_buffer, "reset")) {
        cmd_reset();
    } else if (streq(g_line_buffer, "frames")) {
        cmd_frames();
    } else if (streq(g_line_buffer, "frame")) {
        cmd_frame();
    } else if (streq(g_line_buffer, "unframe")) {
        cmd_unframe();
    } else if (streq(g_line_buffer, "map")) {
        cmd_map();
    } else if (streq(g_line_buffer, "tasks")) {
        cmd_tasks();
    } else if (streq(g_line_buffer, "procs")) {
        cmd_procs();
    } else if (streq(g_line_buffer, "chan")) {
        cmd_chan();
    } else if (streq(g_line_buffer, "send")) {
        cmd_send();
    } else if (streq(g_line_buffer, "ps")) {
        cmd_ps();
    } else if (streq(g_line_buffer, "objs")) {
        cmd_objs();
    } else if (streq(g_line_buffer, "netconns")) {
        cmd_netconns();
    } else if (streq(g_line_buffer, "disk")) {
        cmd_disk();
    } else if (streq(g_line_buffer, "diskwrite")) {
        cmd_disk_write();
    } else if (streq(g_line_buffer, "mkfs")) {
        cmd_mkfs();
    } else if (streq(g_line_buffer, "mkfile")) {
        cmd_mkfile();
    } else if (streq(g_line_buffer, "cat")) {
        cmd_cat();
    } else if (streq(g_line_buffer, "ls")) {
        cmd_ls();
    } else if (starts_with(g_line_buffer, "vfscat ")) {
        cmd_vfs_cat();
    } else if (streq(g_line_buffer, "vfswrite")) {
        cmd_vfs_write();
    } else if (streq(g_line_buffer, "install")) {
        cmd_install();
    } else if (streq(g_line_buffer, "spawn")) {
        cmd_spawn();
    } else if (streq(g_line_buffer, "ring3go")) {
        cmd_ring3_go();
    } else if (streq(g_line_buffer, "ring3fault")) {
        cmd_ring3_fault();
    } else if (streq(g_line_buffer, "ring3nx")) {
        cmd_ring3_nx();
    } else if (streq(g_line_buffer, "ring3reg")) {
        cmd_ring3_register();
    } else if (streq(g_line_buffer, "ring3unreg")) {
        cmd_ring3_unregister();
    } else if (streq(g_line_buffer, "ring3async")) {
        cmd_ring3_async();
    } else if (streq(g_line_buffer, "ring3asyncwrite")) {
        cmd_ring3_async_write();
    } else if (streq(g_line_buffer, "ring3asyncping")) {
        cmd_ring3_async_ping();
    } else if (streq(g_line_buffer, "ring3asyncdns")) {
        cmd_ring3_async_dns();
    } else if (streq(g_line_buffer, "ring3asynctcp")) {
        cmd_ring3_async_tcp();
    } else if (streq(g_line_buffer, "pci")) {
        cmd_pci();
    } else if (streq(g_line_buffer, "nic")) {
        cmd_nic();
    } else if (streq(g_line_buffer, "fb")) {
        cmd_fb();
    } else if (streq(g_line_buffer, "arp")) {
        cmd_arp();
    } else if (streq(g_line_buffer, "ping")) {
        cmd_ping();
    } else if (streq(g_line_buffer, "dns")) {
        cmd_dns();
    } else if (streq(g_line_buffer, "tcp")) {
        cmd_tcp();
    } else if (starts_with(g_line_buffer, "echo ")) {
        cmd_echo();
    } else if (g_line_len > 0) {
        vga_print("unknown command");
        serial_print("unknown command");
    }
}
