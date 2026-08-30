#pragma once

#include "../../../types.h"

#pragma GCC visibility push(hidden)

// A real registry drivers call into when they actually initialize -
// "Driver registruje uređaj kod Device Managera" from the roadmap's own
// text. Every driver in this kernel is lazily initialized on demand from
// a shell command (mouse/pci/fb/nic), not eagerly at boot, so this
// registry honestly reflects that: a device appears the moment its
// driver actually succeeds, not before. USB and moving drivers to
// userspace are explicitly out of scope (the roadmap's own diagram says
// "kasnije"/"ne moraš to odmah implementirati" for both).
#define MAX_DEVICES 16

#define DEVICE_CATEGORY_PCI 1
#define DEVICE_CATEGORY_PLATFORM 2
#define DEVICE_CATEGORY_INPUT 3

typedef struct {
    bool used;
    char name[32];
    int category;
    // Category-specific: PCI packs (vendor_id << 16 | device_id); INPUT
    // is the IRQ number; PLATFORM is currently unused (0).
    u32 info;
} device_entry;

extern device_entry g_devices[MAX_DEVICES];
extern int g_device_count;

// Real upsert: several drivers here can genuinely (re-)initialize more
// than once in one boot (running mouse/pci/fb/nic twice is normal usage)
// - an existing entry with the same name+category has its info updated
// in place instead of creating a duplicate. Returns the entry's index,
// or -1 if this is a new device and no slot is free.
int device_manager_register(const char* name, int category, u32 info);
bool device_manager_get(int index, char* name_out, int* category_out, u32* info_out);

#pragma GCC visibility pop
