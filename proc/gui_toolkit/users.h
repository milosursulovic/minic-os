#pragma once
#include "core.h"

// Real user-account lookup (Faza I point 14 item 6, syscall 93) - proves
// a uid is a real registered account, unlike a bare number. Item 7
// (permission-gating) is where enforcement actually starts using this.

typedef struct __attribute__((packed)) {
    char* name_out;
    u8* gid_out;
} gt_user_lookup_args;

static __attribute__((unused)) bool gt_user_lookup(u8 uid, char* name_out, u8* gid_out) {
    gt_user_lookup_args args;
    args.name_out = name_out;
    args.gid_out = gid_out;
    return gt_syscall(93, (u64) uid, (u64) &args, 0) != (u64) -1;
}
