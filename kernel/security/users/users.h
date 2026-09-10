#pragma once
#include "../../../types.h"

#pragma GCC visibility push(hidden)

// Real user/group account table (Faza I point 14) - `proc/process.h`'s
// `process.uid` used to be just a bare u8 with nothing backing it
// (syscall 49/sys_setuid let any process set it to any value at all,
// deliberately unhardened). This is the actual account table: item 7
// (permission-gating fs_set_owner/fs_set_mode + vfs_read/vfs_write) is
// where enforcement actually starts leaning on it - sys_setuid itself
// stays untouched by this item, so the existing ring3perms demo (which
// setuid()s to arbitrary unregistered test uids on purpose) keeps
// working unchanged.
#define MAX_USERS 8
#define MAX_GROUPS 8
#define ACCOUNT_NAME_MAX 32

typedef struct {
    bool used;
    char username[ACCOUNT_NAME_MAX];
    u8 uid;
    u8 primary_gid;
} user_account;

typedef struct {
    bool used;
    char groupname[ACCOUNT_NAME_MAX];
    u8 gid;
} group_account;

extern user_account g_users[MAX_USERS];
extern int g_user_count;
extern group_account g_groups[MAX_GROUPS];
extern int g_group_count;

// Seeds real accounts: uid 0 "root" (primary_gid 0, group "root"), uid
// 100 "guest" (primary_gid 100, group "guest") - Unix-ish convention
// (0 = superuser, a low non-zero id = the first real account), capped
// well under 255 since process.uid is a plain u8, unlike real Unix's
// usual 1000. Called once at boot from kmain.c.
void users_init(void);
// Returns the new user's slot index, or -1 if the table is full.
int user_create(const char* username, u8 uid, u8 primary_gid);
bool user_lookup_by_uid(u8 uid, user_account* out);
bool user_lookup_by_name(const char* username, user_account* out);
// Returns the new group's slot index, or -1 if the table is full.
int group_create(const char* groupname, u8 gid);
bool group_lookup_by_gid(u8 gid, group_account* out);

#pragma GCC visibility pop
