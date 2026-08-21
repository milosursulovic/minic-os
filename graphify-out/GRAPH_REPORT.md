# Graph Report - minic-os  (2026-08-21)

## Corpus Check
- Corpus is ~32,886 words - fits in a single context window. You may not need a graph.

## Summary
- 508 nodes · 1383 edges · 29 communities (27 shown, 2 thin omitted)
- Extraction: 72% EXTRACTED · 28% INFERRED · 0% AMBIGUOUS · INFERRED: 393 edges (avg confidence: 0.85)
- Token cost: 103,035 input · 0 output

## Community Hubs (Navigation)
- Shell Commands
- Networking Stack
- Kernel Core & Scheduler
- Disk & Filesystem
- Graphics & Window Server
- Ring3 Native API
- IO Ports & Mouse Driver
- Process Lifecycle Docs
- Known Limitations Docs
- Project Layout Docs
- PCI Enumeration
- Object & Handle Model
- Workspace Overview
- Heap Allocator
- Interrupt Handling Rationale
- Async TCP Worker
- Async IO Worker
- Async DNS/Ping Worker
- QEMU Test Tooling
- Build Toolchain Rationale
- IPC Channels
- Scheduler Rationale Docs
- Drivers Docs
- Hello Service Program
- Init Process Program
- Keyboard Driver
- Build Script

## God Nodes (most connected - your core abstractions)
1. `serial_print()` - 65 edges
2. `vga_print()` - 63 edges
3. `run_command()` - 63 edges
4. `print_hex()` - 39 edges
5. `syscall_dispatch()` - 31 edges
6. `yield()` - 22 edges
7. `_start()` - 21 edges
8. `CLAUDE.md (Kernel Guide)` - 21 edges
9. `do_syscall()` - 18 edges
10. `_start()` - 17 edges

## Surprising Connections (you probably didn't know these)
- `_start()` --calls--> `init_scancode_table()`  [INFERRED]
  kmain.c → drivers/keyboard.c
- `_start()` --calls--> `create_channel()`  [INFERRED]
  kmain.c → proc/channel.c
- `channel_receive()` --calls--> `channel_has_message()`  [INFERRED]
  sched/task.c → proc/channel.c
- `syscall_dispatch()` --calls--> `free_io_request()`  [INFERRED]
  syscall/syscall.c → proc/io_request.c
- `syscall_dispatch()` --calls--> `free_net_ping_request()`  [INFERRED]
  syscall/syscall.c → proc/net_request.c

## Import Cycles
- None detected.

## Hyperedges (group relationships)
- **Async Worker-Task-Pool Pattern** — readme_io_request_c, readme_net_request_c, readme_net_tcp_request_c, readme_async_file_io, readme_async_net_ops [INFERRED 0.85]
- **Milestone Doc-Sync Workflow** — claude_skills_kernel_milestone_skill, readme_overview, minic_os_docs_repo, claude_overview [EXTRACTED 0.90]
- **QEMU Concrete-Assertion Testing Workflow** — claude_agents_kernel_qemu_tester_agent, claude_skills_kernel_qemu_test_skill, claude_didnt_crash_insufficient, claude_overview [EXTRACTED 0.90]

## Communities (29 total, 2 thin omitted)

### Community 0 - "Shell Commands"
Cohesion: 0.13
Nodes (66): serial_print(), vga_print(), vga_putc(), fb_get_pixel(), print_hex(), u8, cmd_alloc(), cmd_arp() (+58 more)

### Community 1 - "Networking Stack"
Cohesion: 0.08
Nodes (53): arp_cache_insert(), arp_cache_lookup(), arp_init(), arp_resolve(), arp_send_request(), u8, ip_equals(), u16 (+45 more)

### Community 2 - "Kernel Core & Scheduler"
Cohesion: 0.09
Nodes (44): u64, u8, idt_init(), pic_remap(), pit_init(), set_idt_entry(), _start(), alloc_frame() (+36 more)

### Community 3 - "Disk & Filesystem"
Cohesion: 0.09
Nodes (36): dir_entry, ata_read_sector(), ata_setup(), ata_wait_drq(), ata_wait_ready(), ata_write_sector(), u32, u8 (+28 more)

### Community 4 - "Graphics & Window Server"
Cohesion: 0.10
Nodes (29): outw(), u16, u32, u8, fb_draw_char(), fb_draw_string(), fb_fill_rect(), fb_put_pixel() (+21 more)

### Community 5 - "Ring3 Native API"
Cohesion: 0.18
Nodes (30): channel, file, mouse_state, i32, u32, u64, window, channel_open() (+22 more)

### Community 6 - "IO Ports & Mouse Driver"
Cohesion: 0.20
Nodes (22): u16, u32, u8, inb(), inl(), inw(), new_line(), outb() (+14 more)

### Community 7 - "Process Lifecycle Docs"
Cohesion: 0.14
Nodes (21): proc/ring3prog_linked.elf (intermediate build artifact), proc/ring3prog.c _start() (pinned via .text.start section), spawn_process(), Asynchronous File Read/Write, Asynchronous Networking (ICMP ping / DNS / TCP fetch), proc/channel.c (IPC channels), handle_close (release a handle without exiting), proc/hello_service.c (+13 more)

### Community 8 - "Known Limitations Docs"
Cohesion: 0.15
Nodes (17): "Didn't Crash" Is Never Sufficient Verification Principle, net/arp.c, disk/ata.c (legacy ATA PIO driver), disk/devfs.c (/devices backend), disk/, net/dns.c, net/e1000.c, File.write() Intermittent -1 Return Bug (data integrity unaffected, unreproduced on demand) (+9 more)

### Community 10 - "Project Layout Docs"
Cohesion: 0.15
Nodes (13): No External Libraries / Everything Hand-Written Rule, mm/frames.c, mm/heap.c, kmain.c, lib/, MiniC-OS Kernel Project, mm/, mm/paging.c (+5 more)

### Community 11 - "PCI Enumeration"
Cohesion: 0.41
Nodes (12): u16, u32, u8, pci_check_device(), pci_config_read_byte(), pci_config_read_dword(), pci_config_read_word(), pci_config_write_dword() (+4 more)

### Community 12 - "Object & Handle Model"
Cohesion: 0.24
Nodes (12): alloc_handle(), alloc_object(), free_handle(), free_object(), u64, u8, spawn_process(), spawn_process_from_path() (+4 more)

### Community 13 - "Workspace Overview"
Cohesion: 0.29
Nodes (10): Whole Static Identity Map Is Currently User-Accessible (temporary simplification), Makefile, ../os-docs/ Sibling Directory Reference, CLAUDE.md (Kernel Guide), kernel-milestone Skill, MiniC Language Dialect (retired), minic-os-docs Sibling Repo, minicc Compiler Repo (retired) (+2 more)

### Community 14 - "Heap Allocator"
Cohesion: 0.44
Nodes (8): block_header, block_at(), u64, heap_free_bytes(), heap_grow(), heap_init(), kalloc(), kfree()

### Community 15 - "Interrupt Handling Rationale"
Cohesion: 0.20
Nodes (10): GP Fault Error Code 0 Diagnostic Signature, interrupt_handler(), isr_common_stub, TSS.RSP0 Is an Absolute Reset Point, Not a Continuation, boot/, boot/boot.s, boot/interrupts.s, isr/isr.c (+2 more)

### Community 17 - "Async TCP Worker"
Cohesion: 0.25
Nodes (5): alloc_net_tcp_request(), u16, u8, free_net_tcp_request(), tcp_worker_entry()

### Community 18 - "Async IO Worker"
Cohesion: 0.32
Nodes (6): alloc_io_request(), alloc_io_request_slot(), alloc_io_write_request(), u32, u8, free_io_request()

### Community 19 - "Async DNS/Ping Worker"
Cohesion: 0.38
Nodes (5): alloc_net_dns_request(), alloc_net_ping_request(), alloc_net_request_slot(), u8, free_net_ping_request()

### Community 20 - "QEMU Test Tooling"
Cohesion: 0.47
Nodes (6): kernel-qemu-tester Agent, qemu-mon.sock (QEMU monitor unix socket), serial.log, kernel-qemu-test Skill, shell/shell.c (cmd_* functions + run_command dispatch), shell/

### Community 21 - "Build Toolchain Rationale"
Cohesion: 0.33
Nodes (6): build.sh, 32-bit ELF Container for 64-bit Code (rationale: QEMU multiboot1 -kernel rejects genuine ELF64), -fPIC Flag Requirement (avoids unrepresentable R_X86_64_32S relocation in ELF32), -fvisibility=hidden Requirement (avoids ELF64-only GOTPCREL syntax as --32 can't parse), #pragma GCC visibility push(hidden)/pop Header Template, [rip+label] Required for Hand-Written-Asm C-Global References

### Community 22 - "IPC Channels"
Cohesion: 0.33
Nodes (4): u64, channel_has_message(), channel_send(), create_channel()

### Community 23 - "Scheduler Rationale Docs"
Cohesion: 0.40
Nodes (5): sleep_ticks(), switch_context/run_ring3_test Save-RSP/Pop/Ret Trick, sched/, sched/switch.s, sched/task.c

### Community 24 - "Drivers Docs"
Cohesion: 0.40
Nodes (5): drivers/, drivers/interrupts_init.c, drivers/io.c, drivers/keyboard.c, drivers/pci.c

### Community 25 - "Hello Service Program"
Cohesion: 0.67
Nodes (3): u64, do_syscall(), _start()

### Community 26 - "Init Process Program"
Cohesion: 0.67
Nodes (3): u64, do_syscall(), _start()

## Ambiguous Edges - Review These
- `minic-os-docs Sibling Repo` → `../os-docs/ Sibling Directory Reference`  [AMBIGUOUS]
  CLAUDE.md · relation: conceptually_related_to

## Knowledge Gaps
- **21 isolated node(s):** `build.sh script`, `Makefile`, `kmain.c`, `types.h`, `boot/boot.s` (+16 more)
  These have ≤1 connection - possible missing edges or undocumented components.
- **2 thin communities (<3 nodes) omitted from report** — run `graphify query` to explore isolated nodes.

## Suggested Questions
_Questions this graph is uniquely positioned to answer:_

- **What is the exact relationship between `minic-os-docs Sibling Repo` and `../os-docs/ Sibling Directory Reference`?**
  _Edge tagged AMBIGUOUS (relation: conceptually_related_to) - confidence is low._
- **Why does `syscall_dispatch()` connect `Object & Handle Model` to `Shell Commands`, `Kernel Core & Scheduler`, `Disk & Filesystem`, `Graphics & Window Server`, `IO Ports & Mouse Driver`, `Kernel Headers`, `Async TCP Worker`, `Async IO Worker`, `Async DNS/Ping Worker`?**
  _High betweenness centrality (0.092) - this node is a cross-community bridge._
- **Why does `yield()` connect `Kernel Core & Scheduler` to `Networking Stack`, `Disk & Filesystem`, `IO Ports & Mouse Driver`, `Object & Handle Model`, `Async TCP Worker`?**
  _High betweenness centrality (0.049) - this node is a cross-community bridge._
- **Why does `serial_print()` connect `Shell Commands` to `Kernel Core & Scheduler`, `Object & Handle Model`, `IO Ports & Mouse Driver`?**
  _High betweenness centrality (0.048) - this node is a cross-community bridge._
- **Are the 63 inferred relationships involving `serial_print()` (e.g. with `interrupt_handler()` and `_start()`) actually correct?**
  _`serial_print()` has 63 INFERRED edges - model-reasoned connections that need verification._
- **Are the 61 inferred relationships involving `vga_print()` (e.g. with `print_hex()` and `cmd_alloc()`) actually correct?**
  _`vga_print()` has 61 INFERRED edges - model-reasoned connections that need verification._
- **Are the 5 inferred relationships involving `run_command()` (e.g. with `_start()` and `serial_print()`) actually correct?**
  _`run_command()` has 5 INFERRED edges - model-reasoned connections that need verification._