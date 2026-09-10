#include "service.h"
#include "../shell_state.h"
#include "../../../kernel/drivers/io/io.h"
#include "../../../kernel/drivers/keyboard/keyboard.h"
#include "../../../kernel/lib/strings.h"
#include "../../../kernel/services/service_manager.h"

// service <start|stop|restart|status> <name> - real generic Service
// Manager surface (kernel/services/service_manager.h). "stop" means "don't
// respawn the next time it exits" (see that header's own comment) - this
// kernel has no way to forcibly kill another process.
void cmd_service(void) {
    char subcmd[32];
    char* name;
    if (!split_two_args(&g_line_buffer[8], subcmd, &name)) {  // past "service "
        return;
    }
    if (streq(subcmd, "start")) {
        bool ok = service_start(name);
        vga_print(ok ? "service started" : "service start failed (unknown name?)");
        serial_print(ok ? "service started" : "service start failed (unknown name?)");
    } else if (streq(subcmd, "stop")) {
        bool ok = service_stop(name);
        vga_print(ok ? "service stopped (will not auto-restart)" : "service stop failed (unknown name?)");
        serial_print(ok ? "service stopped (will not auto-restart)" : "service stop failed (unknown name?)");
    } else if (streq(subcmd, "restart")) {
        bool ok = service_restart(name);
        vga_print(ok ? "service restarted" : "service restart failed (unknown name?)");
        serial_print(ok ? "service restarted" : "service restart failed (unknown name?)");
    } else if (streq(subcmd, "status")) {
        bool running;
        u32 restart_count;
        int process_index;
        if (!service_get_status(name, &running, &restart_count, &process_index)) {
            vga_print("service status failed (unknown name?)");
            serial_print("service status failed (unknown name?)");
            return;
        }
        vga_print("running=0x");
        serial_print("running=0x");
        print_hex((u64) running);
        vga_print(" restart_count=0x");
        serial_print(" restart_count=0x");
        print_hex((u64) restart_count);
        vga_print(" process_index=0x");
        serial_print(" process_index=0x");
        print_hex((u64) (process_index < 0 ? 0xFFFFFFFF : (u32) process_index));
    } else {
        vga_print("usage: service <start|stop|restart|status> <name>");
        serial_print("usage: service <start|stop|restart|status> <name>");
    }
}
