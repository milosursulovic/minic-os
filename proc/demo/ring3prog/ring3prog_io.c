#include "ring3prog_common.h"

// Extracted from the former single ring3prog.c (1380 lines) - see that
// file's own header comment and ring3prog_common.h for the split rationale.
// Returns true if trigger_value matched one of this group's own triggers
// (and was therefore handled - some branches below never return at all,
// looping forever, exactly as in the original single-file version).
bool run_trigger_io(u64 trigger_value) {
    if (trigger_value == 6) {
        // trigger 6 (ring3async): async read, do work before waiting.
        u64 async_handle = do_syscall(16, (u64) "/system/ring3msg.txt", 0, 0);
        do_syscall(1, (u64) "file_read_async() got handle 0x", async_handle, 0);

        int i = 0;
        while (i < 5) {
            do_syscall(1, (u64) "doing other work, iteration 0x", (u64) i, 0);
            i = i + 1;
        }

        u64 async_result = do_syscall(17, async_handle, (u64) &g_async_buf[0], 63);
        g_async_buf[async_result] = 0;
        do_syscall(1, (u64) "file_read_wait() got back 0x", async_result, 0);
        do_syscall(1, (u64) &g_async_buf[0], 0, 0);
    } else if (trigger_value == 7) {
        // trigger 7 (ring3asyncwrite): async write, then verify via sync read.
        char* write_payload = "async write via a real worker task, not a stub!";
        u64 write_handle = do_syscall(18, (u64) "/system/ring3asyncmsg.txt", (u64) write_payload, 47);
        do_syscall(1, (u64) "file_write_async() got handle 0x", write_handle, 0);

        int j = 0;
        while (j < 5) {
            do_syscall(1, (u64) "doing other work, iteration 0x", (u64) j, 0);
            j = j + 1;
        }

        u64 write_result = do_syscall(19, write_handle, 0, 0);
        do_syscall(1, (u64) "file_write_wait() got back 0x", write_result, 0);

        file async_written_file;
        async_written_file.path = "/system/ring3asyncmsg.txt";
        u64 verify_read = file_read(&async_written_file, (char*) &g_async_buf[0], 63);
        g_async_buf[verify_read] = 0;
        do_syscall(1, (u64) "verify File.read() got back 0x", verify_read, 0);
        do_syscall(1, (u64) &g_async_buf[0], 0, 0);
    } else if (trigger_value == 8) {
        // trigger 8 (ring3asyncping): async ping to the gateway.
        u64 gateway_ip = 0x0A000202;  // 10.0.2.2
        u64 ping_handle = do_syscall(20, gateway_ip, 0, 0);
        do_syscall(1, (u64) "net_ping_async() got handle 0x", ping_handle, 0);

        int k = 0;
        while (k < 5) {
            do_syscall(1, (u64) "doing other work, iteration 0x", (u64) k, 0);
            k = k + 1;
        }

        u64 ping_result = do_syscall(21, ping_handle, 0, 0);
        do_syscall(1, (u64) "net_ping_wait() got ok=0x", ping_result, 0);
    } else if (trigger_value == 9) {
        // trigger 9 (ring3asyncdns): async DNS resolve.
        u64 dns_handle = do_syscall(22, (u64) "example.com", 0, 0);
        do_syscall(1, (u64) "net_dns_async() got handle 0x", dns_handle, 0);

        int m = 0;
        while (m < 5) {
            do_syscall(1, (u64) "doing other work, iteration 0x", (u64) m, 0);
            m = m + 1;
        }

        u8 resolved_ip[4];
        u64 dns_result = do_syscall(23, dns_handle, (u64) &resolved_ip[0], 0);
        do_syscall(1, (u64) "net_dns_wait() got ok=0x", dns_result, 0);
        if (dns_result != 0) {
            u64 packed_ip = ((u64) resolved_ip[0] << 24) | ((u64) resolved_ip[1] << 16)
                | ((u64) resolved_ip[2] << 8) | (u64) resolved_ip[3];
            do_syscall(1, (u64) "resolved IP (packed) 0x", packed_ip, 0);
        }
    } else if (trigger_value == 10) {
        // trigger 10 (ring3asynctcp): chains async DNS resolve into async TCP fetch.
        u64 dns_handle = do_syscall(22, (u64) "example.com", 0, 0);
        do_syscall(1, (u64) "net_dns_async() got handle 0x", dns_handle, 0);
        u8 resolved_ip[4];
        u64 dns_result = do_syscall(23, dns_handle, (u64) &resolved_ip[0], 0);
        do_syscall(1, (u64) "net_dns_wait() got ok=0x", dns_result, 0);

        if (dns_result != 0) {
            u64 packed_ip = ((u64) resolved_ip[0] << 24) | ((u64) resolved_ip[1] << 16)
                | ((u64) resolved_ip[2] << 8) | (u64) resolved_ip[3];
            u64 packed_ip_and_port = (packed_ip << 16) | 80;
            char* request = "GET / HTTP/1.1\r\nHost: example.com\r\nConnection: close\r\n\r\n";
            u64 tcp_handle = do_syscall(24, packed_ip_and_port, (u64) request, 58);
            do_syscall(1, (u64) "net_tcp_fetch_async() got handle 0x", tcp_handle, 0);

            int n = 0;
            while (n < 5) {
                do_syscall(1, (u64) "doing other work, iteration 0x", (u64) n, 0);
                n = n + 1;
            }

            u64 tcp_result = do_syscall(25, tcp_handle, (u64) &g_async_buf[0], 63);
            do_syscall(1, (u64) "net_tcp_fetch_wait() got response_len=0x", tcp_result, 0);
            if (tcp_result != (u64) -1) {
                g_async_buf[tcp_result] = 0;
                do_syscall(1, (u64) &g_async_buf[0], 0, 0);
            }
        }
    } else if (trigger_value == 15) {
        // trigger 15 (ring3fileobj): real persistent File kernel objects
        // (gui_toolkit.h's gt_file_*, syscalls 44-48) - incremental
        // cursor-advancing reads, enforced READ/WRITE handle rights, and
        // buffered-write-commit-on-close, unlike syscalls 4/5's one-shot
        // vfs_read/vfs_write.
        int read_handle = gt_file_open("/system/file0.mfs", 0);
        do_syscall(1, (u64) "file_open(file0.mfs, read) handle=0x", (u64) read_handle, 0);

        u8 chunk1[4];
        int n1 = gt_file_read(read_handle, chunk1, 4);
        do_syscall(1, (u64) "read #1 n=0x", (u64) n1, 0);

        u8 chunk2[64];
        int n2 = gt_file_read(read_handle, chunk2, 64);
        do_syscall(1, (u64) "read #2 n=0x", (u64) n2, 0);

        u8 bogus_byte = 0x41;
        int write_on_readonly = gt_file_write(read_handle, &bogus_byte, 1);
        do_syscall(1, (u64) "write() on a read-only handle result=0x", (u64) write_on_readonly, 0);

        gt_file_close(read_handle);

        int write_handle = gt_file_open("/system/fileobjtest.mfs", 1);
        do_syscall(1, (u64) "file_open(fileobjtest.mfs, write) handle=0x", (u64) write_handle, 0);
        const char* part1 = "Hello ";
        const char* part2 = "File Objects!";
        gt_file_write(write_handle, (const u8*) part1, 6);
        gt_file_write(write_handle, (const u8*) part2, 13);
        bool closed_ok = gt_file_close(write_handle);
        do_syscall(1, (u64) "close(write) ok=0x", (u64) closed_ok, 0);

        int readback_handle = gt_file_open("/system/fileobjtest.mfs", 0);
        u8 readback[32];
        int n3 = gt_file_read(readback_handle, readback, 31);
        readback[n3] = 0;
        do_syscall(1, (u64) "readback n=0x", (u64) n3, 0);
        do_syscall(1, (u64) &readback[0], 0, 0);
        gt_file_close(readback_handle);
    } else if (trigger_value == 17) {
        // trigger 17 (ring3posix): a real POSIX shim (proc/posix/posix.h)
        // - open/read/write/close/lseek, over the exact same syscalls
        // 44-48 as gui_toolkit.h's gt_file_* wrappers.
        int wfd = open("/system/posixtest.mfs", O_WRONLY);
        int wn = write(wfd, "posix works", 11);
        do_syscall(1, (u64) "open(O_WRONLY) fd=0x", (u64) wfd, 0);
        do_syscall(1, (u64) "write() n=0x", (u64) wn, 0);
        close(wfd);

        int rfd = open("/system/posixtest.mfs", O_RDONLY);
        u8 posix_buf[32];
        int rn = read(rfd, posix_buf, 31);
        posix_buf[rn] = 0;
        do_syscall(1, (u64) "read() n=0x", (u64) rn, 0);
        do_syscall(1, (u64) &posix_buf[0], 0, 0);

        int seek_result = lseek(rfd, 0, SEEK_SET);
        int rn2 = read(rfd, posix_buf, 31);
        posix_buf[rn2] = 0;
        do_syscall(1, (u64) "lseek() result=0x", (u64) seek_result, 0);
        do_syscall(1, (u64) &posix_buf[0], 0, 0);
        close(rfd);

        int missing_fd = open("/system/does_not_exist.mfs", O_RDONLY);
        do_syscall(1, (u64) "open(nonexistent) fd=0x", (u64) missing_fd, 0);
    } else if (trigger_value == 18) {
        // trigger 18 (ring3pipe): real byte-stream Pipe (proc/ipc/pipe/
        // pipe.h) - the shell command that sends this trigger already
        // wrote several separate short strings directly into the
        // well-known boot-time pipe (kernel-side, no syscall needed)
        // before sending it, so a genuine multi-write reassembly is
        // being read back here, not just one clean write echoed back.
        int pipe_handle = gt_pipe_open(0, 0);  // raw index 0 = g_ring3_pipe_demo, receive-only
        do_syscall(1, (u64) "pipe_open(receive) handle=0x", (u64) pipe_handle, 0);

        u8 chunk1[6];
        int n1 = gt_pipe_read(pipe_handle, chunk1, 6);
        chunk1[n1] = 0;
        do_syscall(1, (u64) "read #1 n=0x", (u64) n1, 0);
        do_syscall(1, (u64) &chunk1[0], 0, 0);

        u8 chunk2[32];
        int n2 = gt_pipe_read(pipe_handle, chunk2, 32);
        chunk2[n2] = 0;
        do_syscall(1, (u64) "read #2 n=0x", (u64) n2, 0);
        do_syscall(1, (u64) &chunk2[0], 0, 0);
    } else if (trigger_value == 34) {
        // trigger 34 (ring3posix2) - Faza I point 13, item 11: POSIX
        // completeness. Proves O_RDWR (real random-access read+write on
        // one fd), SEEK_CUR/SEEK_END, a real errno, and unlink()/stat()/
        // fcntl(), all in one self-contained run.
        const char* path = "/system/posix2test.mfs";
        const char* original = "abcdefghijklmnopqrst";  // 20 bytes, a..t

        int wfd = open(path, O_WRONLY);
        write(wfd, original, 20);
        close(wfd);

        int fd = open(path, O_RDWR);
        do_syscall(1, (u64) "open(O_RDWR) fd=0x", (u64) fd, 0);

        i64 end_pos = lseek(fd, 0, SEEK_END);
        do_syscall(1, (u64) "lseek(SEEK_END, 0)=0x", (u64) end_pos, 0);

        i64 back_pos = lseek(fd, -5, SEEK_CUR);
        do_syscall(1, (u64) "lseek(SEEK_CUR, -5)=0x", (u64) back_pos, 0);

        int wn = write(fd, "12345", 5);
        do_syscall(1, (u64) "in-place write() n=0x", (u64) wn, 0);

        lseek(fd, 0, SEEK_SET);
        u8 readback[24];
        int rn = read(fd, readback, 20);
        readback[rn] = 0;
        do_syscall(1, (u64) "readback n=0x", (u64) rn, 0);
        do_syscall(1, (u64) &readback[0], 0, 0);  // expected: abcdefghijklmno12345

        int flags = fcntl(fd, F_GETFL);
        do_syscall(1, (u64) "fcntl(F_GETFL)=0x", (u64) flags, 0);

        close(fd);

        stat_t st;
        int stat_ok = stat(path, &st);
        do_syscall(1, (u64) "stat() ok=0x", (u64) stat_ok, 0);
        do_syscall(1, (u64) "stat() st_size=0x", (u64) st.st_size, 0);

        int unlink_ok = unlink(path);
        do_syscall(1, (u64) "unlink() ok=0x", (u64) unlink_ok, 0);

        stat_t st2;
        int stat_after_unlink = stat(path, &st2);
        do_syscall(1, (u64) "stat(after unlink) result=0x", (u64) stat_after_unlink, 0);
        do_syscall(1, (u64) "errno=0x", (u64) errno, 0);
    } else {
        return false;
    }
    return true;
}
