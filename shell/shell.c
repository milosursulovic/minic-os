// The minimal interactive shell, built on keyboard.c's line buffer. The
// main loop (kmain.c's _start) dispatches a line via run_command() once
// keyboard.c's IRQ1 handler (isr.c) sets g_line_ready.
//
// Grows a `cmd_*` + dispatch branch per subsystem as each one gets
// ported (mm, sched, syscall/proc/net, disk) - kept minimal here through
// Stage 1 (interrupts/keyboard skeleton only).

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

#pragma GCC visibility push(hidden)
extern u8 g_test_prog_start;
extern u8 g_test_prog_end;
#pragma GCC visibility pop

void print_prompt(void) {
    vga_print("> ");
    serial_print("> ");
}

static void cmd_help(void) {
    vga_print("commands: help clear ticks alloc bigalloc free free <addr> mem reset frame unframe frames map tasks procs ps objs chan send disk diskwrite mkfs mkfile cat ls vfscat <path> vfswrite install spawn ring3go ring3fault ring3nx pci nic arp ping dns tcp echo <text>");
    serial_print("commands: help clear ticks alloc bigalloc free free <addr> mem reset frame unframe frames map tasks procs ps objs chan send disk diskwrite mkfs mkfile cat ls vfscat <path> vfswrite install spawn ring3go ring3fault ring3nx pci nic arp ping dns tcp echo <text>\n");
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
        serial_print("alloc failed - heap full\n");
    } else {
        g_last_alloc = p;
        vga_print("allocated 64 bytes at 0x");
        serial_print("allocated 64 bytes at 0x");
        print_hex((u64) p);
    }
}

static void cmd_big_alloc(void) {
    // 64KB - bigger than a single heap_grow() chunk, so this reliably
    // forces at least one on-demand growth cycle in one shot, instead of
    // needing dozens of plain `alloc`s to exhaust the initial mapping.
    void* p = kalloc(65536);
    if (p == NULL) {
        vga_print("bigalloc failed - heap full");
        serial_print("bigalloc failed - heap full\n");
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
        serial_print("nothing to free\n");
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
    serial_print("heap reset\n");
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
        serial_print("out of frames\n");
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
        serial_print("nothing to unframe\n");
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
        serial_print("out of frames\n");
        return;
    }
    u64 vaddr = 0x40000000;  // 1GB - just past boot.s's static identity map
    bool ok = map_page(vaddr, (u64) frame, 0x02 | PAGE_NX);  // writable, non-executable
    if (!ok) {
        vga_print("map failed");
        serial_print("map failed\n");
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
        serial_print("send failed - channel full\n");
        return;
    }
    vga_print("sent 0xc0ffee1234");
    serial_print("sent 0xc0ffee1234\n");
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

// Reads LBA 1, a sector the disk image is pre-populated with (from the
// host side, before boot) with a known ASCII signature followed by
// zero-fill. Printing it as a string is safe precisely because of that
// zero-fill: the byte right after the signature is a real null
// terminator, not luck.
static void cmd_disk(void) {
    u8 buf[512];
    bool ok = ata_read_sector(1, buf);
    if (!ok) {
        vga_print("disk read failed");
        serial_print("disk read failed\n");
        return;
    }
    char* s = (char*) &buf[0];
    vga_print("sector 1: ");
    serial_print("sector 1: ");
    vga_print(s);
    serial_print(s);
}

// Writes a fixed pattern to LBA 100 (arbitrary, clear of the signature
// sector) and immediately reads it back into a SEPARATE buffer -
// comparing the two proves a real round trip through the driver.
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
        serial_print("disk write failed\n");
        return;
    }
    u8 read_buf[512];
    bool read_ok = ata_read_sector(100, read_buf);
    if (!read_ok) {
        vga_print("disk write ok, readback failed");
        serial_print("disk write ok, readback failed\n");
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
        serial_print("write+readback verified, 512/512 bytes match\n");
    } else {
        vga_print("MISMATCH - write or read is broken");
        serial_print("MISMATCH - write or read is broken\n");
    }
}

static void cmd_mkfs(void) {
    bool ok = mkfs();
    if (ok) {
        vga_print("filesystem formatted");
        serial_print("filesystem formatted\n");
    } else {
        vga_print("mkfs failed");
        serial_print("mkfs failed\n");
    }
}

static int g_next_file_index;
static char g_last_file_name[20];

// Creates a new file every call, name and content both embedding the
// same running index ("file0.mfs" / "file1.mfs" / ...) - running this
// twice and `cat`-ing after each one is what proves multiple files
// coexist correctly and that the free-space scan advances past each
// file already written.
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
        serial_print("mkfile failed\n");
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
        serial_print("no file yet - run mkfile first\n");
        return;
    }
    u8 buf[65];
    int n = fs_read_file(&g_last_file_name[0], buf, 64);
    if (n < 0) {
        vga_print("cat failed");
        serial_print("cat failed\n");
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
        serial_print("ls failed - disk read error\n");
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

// Reads an arbitrary path through the VFS - "vfscat /system/file0.mfs"
// routes to MiniFS (real disk I/O), "vfscat /devices/ticks" routes to
// devfs (live kernel state, no disk touched at all). Same function
// call, two completely different mechanisms depending only on the path
// prefix.
static void cmd_vfs_cat(void) {
    char* path = &g_line_buffer[7];  // past "vfscat "
    u8 buf[256];
    int n = vfs_read(path, buf, 256);
    if (n == -2) {
        vga_print("vfscat: file too large to display");
        serial_print("vfscat: file too large to display\n");
        return;
    }
    if (n < 0) {
        vga_print("vfscat: not found");
        serial_print("vfscat: not found\n");
        return;
    }
    buf[n] = 0;
    char* s = (char*) &buf[0];
    vga_print(s);
    serial_print(s);
}

// Writes a fixed demo file through the VFS (not fs_write_file()
// directly) to prove the write side routes too, not just reads.
static void cmd_vfs_write(void) {
    const char* content = "This file was written through the VFS layer, not MiniFS directly.";
    int len = strlen_(content) + 1;  // include the null terminator, same as mkfile's content
    bool ok = vfs_write("/system/vfsdemo.mfs", (u8*) content, (u32) len);
    if (ok) {
        vga_print("wrote /system/vfsdemo.mfs via VFS");
        serial_print("wrote /system/vfsdemo.mfs via VFS\n");
    } else {
        vga_print("vfswrite failed");
        serial_print("vfswrite failed\n");
    }
}

// Writes the kernel's own compiled-in test program (proc/ring3blob.s,
// g_test_prog_start..g_test_prog_end) out to a real MiniFS file,
// simulating "this program is now genuinely installed on disk,"
// addressable by path and indistinguishable from any other file.
// `spawn` is what actually proves the load-from-disk path; `install`
// just gets real bytes onto real storage first.
static void cmd_install(void) {
    u32 len = (u32) ((u64) &g_test_prog_end - (u64) &g_test_prog_start);
    bool ok = vfs_write("/system/testprog.bin", &g_test_prog_start, len);
    if (!ok) {
        vga_print("install failed");
        serial_print("install failed\n");
        return;
    }
    vga_print("installed /system/testprog.bin, 0x");
    serial_print("installed /system/testprog.bin, 0x");
    print_hex((u64) len);
    vga_print(" bytes");
    serial_print(" bytes");
}

// Reads /system/testprog.bin back through the VFS and spawns a
// brand-new isolated ring3 process from THOSE bytes - a second,
// independent instance of the same program, loaded from disk this time
// rather than the kernel's own compiled-in image.
static void cmd_spawn(void) {
    int idx = spawn_process_from_path("/system/testprog.bin", 0x80000000, 0x80020000);
    if (idx < 0) {
        vga_print("spawn failed");
        serial_print("spawn failed\n");
        return;
    }
    vga_print("spawned process 0x");
    serial_print("spawned process 0x");
    print_hex((u64) idx);
}

// Wakes the boot-time ring3 process's own blocking Channel.receive()
// call. Once the ring3 process receives this, it goes on to call
// Process.spawn() - run `install` first so the file it spawns actually
// exists on disk.
static void cmd_ring3_go(void) {
    bool ok = channel_send(g_ring3_channel_demo, 0x1);
    if (!ok) {
        vga_print("ring3go failed - channel full");
        serial_print("ring3go failed - channel full\n");
        return;
    }
    vga_print("sent ring3 spawn trigger");
    serial_print("sent ring3 spawn trigger\n");
}

// Sends trigger value 0x2 (distinct from ring3go's 0x1) - the boot-time
// ring3 process branches on this to attempt a deliberate forbidden
// write instead of spawning. ONE-SHOT, KERNEL-HALTING: if the fix in
// mm/paging.c's clone_address_space() is working, the write takes a
// real page fault and the kernel halts right there - run it in its own
// dedicated session, never interleaved with other regression testing.
static void cmd_ring3_fault(void) {
    bool ok = channel_send(g_ring3_channel_demo, 0x2);
    if (!ok) {
        vga_print("ring3fault failed - channel full");
        serial_print("ring3fault failed - channel full\n");
        return;
    }
    vga_print("sent ring3 forbidden-write trigger - expect a page fault");
    serial_print("sent ring3 forbidden-write trigger - expect a page fault\n");
}

// Sends trigger value 0x3 - the boot-time ring3 process branches on
// this to write a `ret` opcode onto its own user stack and attempt to
// execute it, proving PAGE_NX really is enforced there. Also ONE-SHOT
// and KERNEL-HALTING.
static void cmd_ring3_nx(void) {
    bool ok = channel_send(g_ring3_channel_demo, 0x3);
    if (!ok) {
        vga_print("ring3nx failed - channel full");
        serial_print("ring3nx failed - channel full\n");
        return;
    }
    vga_print("sent ring3 stack-execution trigger - expect a page fault");
    serial_print("sent ring3 stack-execution trigger - expect a page fault\n");
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

// Milestone 29: enumerates every PCI device on bus 0 and prints its
// address (bus:device.function) plus vendor/device/class/subclass -
// the first time this kernel discovers its own hardware instead of
// everything being a fixed, hardcoded I/O port.
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

// Milestone 30: initializes the e1000 NIC the `pci` command already
// found (enables it over PCI, maps its MMIO register file) and reads
// back real hardware state - its actual MAC address (from RAL0/RAH0,
// pre-loaded by QEMU's emulated EEPROM the same way real hardware
// auto-loads its burned-in address) and its link-up status.
static void cmd_nic(void) {
    bool ok = e1000_init();
    if (!ok) {
        vga_print("e1000 init failed - device not found at 0:3.0");
        serial_print("e1000 init failed - device not found at 0:3.0\n");
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

// Milestone 32: exercises the real resolver (net/arp.c). Four real,
// independently checkable claims in one command:
//   1. resolve the gateway (10.0.2.2).
//   2. resolve it AGAIN - a real cache hit, checkable by real elapsed
//      ticks: the first call sends a packet and waits for an external
//      reply (nonzero elapsed ticks), the second returns from the cache
//      alone (zero packets sent, zero elapsed ticks) - a genuine
//      behavioral difference, not just "returned the same value twice."
//   3. resolve a SECOND, different real address (10.0.2.3, QEMU
//      SLIRP's own well-known built-in DNS proxy) - proves the resolver
//      genuinely generalizes past one fixed address.
//   4. resolve an address nothing answers for (10.0.2.99) - a real
//      negative-space proof: arp_resolve() must return false after its
//      own bounded timeout, not hang forever or return garbage.
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

// Milestone 33: a real IPv4 layer, verified with a genuine ICMP echo
// (ping) round trip to the gateway - the minimal, natural "does IP
// actually work end to end" test. Resolves the gateway's MAC first
// (reusing arp.c's real resolver - a cache hit costs nothing here),
// builds and sends a real Ethernet+IPv4+ICMP echo request, and polls
// for a genuinely matching reply: right EtherType, right IP protocol,
// right source IP, right ICMP type, AND the exact identifier/sequence
// this request sent.
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

// Milestone 34: a real UDP layer, verified with a genuine DNS query to
// QEMU SLIRP's built-in DNS proxy (10.0.2.3) - the minimal, natural
// verification vehicle for "does UDP actually work end to end." Not a
// general DNS client (no compression, no caching) - one hardcoded
// query, purely as a real external stimulus.
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

// Milestone 35: a real TCP client - the first genuinely stateful
// transport protocol in this kernel, verified with a genuine HTTP GET
// round trip to a real internet host (not QEMU SLIRP's own gateway/DNS
// proxy - the first time any protocol layer here has talked to
// something off the local subnet, routed through the gateway at the
// link layer the way a real off-subnet destination has to be). Resolves
// example.com's real A record first (net/dns.c's dns_resolve_a() - a
// dynamic lookup, not a hardcoded IP that could go stale), then
// connects, sends a real `Connection: close` HTTP/1.1 request, and
// reports whether a genuine `HTTP/1.1 ...` status line came back - not
// just "some bytes arrived."
static void cmd_tcp(void) {
    u8 ip[4];
    if (!dns_resolve_a("example.com", &ip[0])) {
        vga_print("tcp: could not resolve example.com");
        serial_print("tcp: could not resolve example.com\n");
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
    } else if (streq(g_line_buffer, "pci")) {
        cmd_pci();
    } else if (streq(g_line_buffer, "nic")) {
        cmd_nic();
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
        serial_print("unknown command\n");
    }
}
