// Milestone 18: a VFS abstraction over milestone 17's MiniFS - the last
// step of the storage phase. Before this, the shell called
// fsReadFile/fsWriteFile directly; there was no path namespace, no way
// to route a request to more than one backend, and no way to plug in a
// second filesystem implementation even in principle. A basic namespace
// (`/system`, `/devices`, ...) fixes that: `vfsRead`/`vfsWrite` take a
// path, find which mount prefix it falls under, strip the prefix, and
// dispatch to whichever backend owns that mount - tag + if/else, the
// same dispatch style `proc/object.mc`'s KernelObject.type already
// established in this kernel, not function pointers (untested in this
// codebase's freestanding/no-register-allocation constraints, and this
// milestone's actual point is proving routing works, not exercising a
// different MiniC feature).
//
// The real proof this is a genuine abstraction, not just a renamed API:
// two backends exist from day one. `/system` routes to MiniFS (real
// disk I/O). `/devices` (disk/devfs.mc) routes to live kernel state -
// nothing touches the disk at all. The exact same `vfsRead()` call
// reaching two completely different mechanisms depending only on the
// path prefix is what "VFS" actually means.

import "minifs.mc";
import "devfs.mc";
import "../lib/strings.mc";

u32 BACKEND_MINIFS = 1;
u32 BACKEND_DEVICE = 2;

struct Mount {
    char prefix[16];
    u32 backend;
    bool used;
}

Mount gMounts[4];
int gMountCount;

void copyPrefix(char* dst, char* src) {
    int i = 0;
    while (i < 15 && src[i] != '\0') {
        dst[i] = src[i];
        i = i + 1;
    }
    while (i < 16) {
        dst[i] = '\0';
        i = i + 1;
    }
}

bool vfsMount(char* prefix, u32 backend) {
    if (gMountCount >= 4) {
        return false;
    }
    copyPrefix(gMounts[gMountCount].prefix, prefix);
    gMounts[gMountCount].backend = backend;
    gMounts[gMountCount].used = true;
    gMountCount = gMountCount + 1;
    return true;
}

int vfsFindMount(char* path) {
    int i = 0;
    while (i < gMountCount) {
        if (gMounts[i].used && startsWith(path, gMounts[i].prefix)) {
            return i;
        }
        i = i + 1;
    }
    return -1;
}

// Strips a matched mount's prefix (and the separating '/', if present)
// off a path - "/system/hello.txt" through the "/system" mount becomes
// "hello.txt", exactly the bare name MiniFS's own API expects.
char* vfsStripPrefix(char* path, int mountIndex) {
    int prefixLen = strlen(gMounts[mountIndex].prefix);
    char* rest = &path[prefixLen];
    if (rest[0] == '/') {
        rest = &rest[1];
    }
    return rest;
}

int vfsRead(char* path, u8* buf, u32 maxLen) {
    int m = vfsFindMount(path);
    if (m < 0) {
        return -1;
    }
    char* rest = vfsStripPrefix(path, m);
    if (gMounts[m].backend == BACKEND_MINIFS) {
        return fsReadFile(rest, buf, maxLen);
    }
    if (gMounts[m].backend == BACKEND_DEVICE) {
        return deviceRead(rest, buf, maxLen);
    }
    return -1;
}

// Device pseudo-files are read-only (they reflect live kernel state -
// there's nothing meaningful to write back into gTickCount by writing
// "/devices/ticks"), so only a MiniFS-backed mount ever accepts a write.
bool vfsWrite(char* path, u8* data, u32 len) {
    int m = vfsFindMount(path);
    if (m < 0) {
        return false;
    }
    char* rest = vfsStripPrefix(path, m);
    if (gMounts[m].backend == BACKEND_MINIFS) {
        return fsWriteFile(rest, data, len);
    }
    return false;
}
