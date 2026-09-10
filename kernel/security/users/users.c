#include "users.h"
#include "../../lib/strings.h"

user_account g_users[MAX_USERS];
int g_user_count;
group_account g_groups[MAX_GROUPS];
int g_group_count;

static void copy_account_name(char* dst, const char* src) {
    int i = 0;
    while (src[i] != '\0' && i < ACCOUNT_NAME_MAX - 1) {
        dst[i] = src[i];
        i = i + 1;
    }
    dst[i] = '\0';
}

int user_create(const char* username, u8 uid, u8 primary_gid) {
    if (g_user_count >= MAX_USERS) {
        return -1;
    }
    int slot = g_user_count;
    g_users[slot].used = true;
    copy_account_name(g_users[slot].username, username);
    g_users[slot].uid = uid;
    g_users[slot].primary_gid = primary_gid;
    g_user_count = g_user_count + 1;
    return slot;
}

bool user_lookup_by_uid(u8 uid, user_account* out) {
    int i = 0;
    while (i < g_user_count) {
        if (g_users[i].used && g_users[i].uid == uid) {
            *out = g_users[i];
            return true;
        }
        i = i + 1;
    }
    return false;
}

bool user_lookup_by_name(const char* username, user_account* out) {
    int i = 0;
    while (i < g_user_count) {
        if (g_users[i].used && streq(g_users[i].username, username)) {
            *out = g_users[i];
            return true;
        }
        i = i + 1;
    }
    return false;
}

int group_create(const char* groupname, u8 gid) {
    if (g_group_count >= MAX_GROUPS) {
        return -1;
    }
    int slot = g_group_count;
    g_groups[slot].used = true;
    copy_account_name(g_groups[slot].groupname, groupname);
    g_groups[slot].gid = gid;
    g_group_count = g_group_count + 1;
    return slot;
}

bool group_lookup_by_gid(u8 gid, group_account* out) {
    int i = 0;
    while (i < g_group_count) {
        if (g_groups[i].used && g_groups[i].gid == gid) {
            *out = g_groups[i];
            return true;
        }
        i = i + 1;
    }
    return false;
}

void users_init(void) {
    group_create("root", 0);
    group_create("guest", 100);
    user_create("root", 0, 0);
    user_create("guest", 100, 100);
}
