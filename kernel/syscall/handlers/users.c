#include "users.h"
#include "../../security/users/users.h"

typedef struct __attribute__((packed)) {
    char* name_out;
    u8* gid_out;
} user_lookup_args;

bool syscall_users(u64 num, u64 a1, u64 a2, u64 a3, u64* result) {
    (void) a3;
    if (num == 93) {
        u8 uid = (u8) a1;
        user_lookup_args* args = (user_lookup_args*) a2;
        user_account account;
        if (!user_lookup_by_uid(uid, &account)) {
            *result = (u64) -1;
            return true;
        }
        int i = 0;
        while (account.username[i] != '\0') {
            args->name_out[i] = account.username[i];
            i = i + 1;
        }
        args->name_out[i] = '\0';
        *args->gid_out = account.primary_gid;
        *result = 0;
        return true;
    }
    return false;
}
