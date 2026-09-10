#include "service_manager.h"
#include "../../services/service_manager.h"

typedef struct __attribute__((packed)) {
    int index;
    char* name_out;
    u32* flags_out;
    u32* restart_count_out;
} service_list_args;

bool syscall_service_manager(u64 num, u64 a1, u64 a2, u64 a3, u64* result) {
    (void) a2;
    (void) a3;
    if (num == 65) {
        service_list_args* args = (service_list_args*) a1;
        bool ok = service_list_entry(args->index, args->name_out, args->flags_out, args->restart_count_out);
        *result = (u64) ok;
        return true;
    }
    if (num == 66) {
        char* name = (char*) a1;
        bool ok = service_start(name);
        *result = (u64) ok;
        return true;
    }
    if (num == 67) {
        char* name = (char*) a1;
        bool ok = service_stop(name);
        *result = (u64) ok;
        return true;
    }
    if (num == 68) {
        char* name = (char*) a1;
        bool ok = service_restart(name);
        *result = (u64) ok;
        return true;
    }
    return false;
}
