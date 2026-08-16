// Milestone 14: a generic kernel object model + per-process handle
// tables - the NT-style piece of the long-term architecture plan.
// Deliberately not introduced any earlier: a handle only means something
// once there's a real user/kernel boundary worth protecting (milestone
// 11) and real per-process state worth wrapping (milestones 12/13) -
// before that, everything was ring 0 and saw everything anyway, so a
// handle indirection would have been pure ceremony.
//
// Two tables, matching the real NT design this is modeled on: a single
// kernel-wide object table (gObjects) holding the actual objects, and a
// SEPARATE table per process (gHandleTables) of small integers that
// index into it. Ring3 code only ever sees the small integer - it can
// never walk straight into gObjects[] itself, since resolveHandle() is
// the one place that decides whether a given process's handle N refers
// to anything at all.

// MiniC has no `const` and enum member values aren't wired to codegen
// yet - a plain global, by convention never reassigned after this line,
// same as every other "constant" in this codebase (gHeapBase, etc.).
// No OBJ_NONE constant - an unallocated KernelObject's zero-valued
// `type` field is never read (guarded by `used` everywhere), so there's
// nothing for a "none" tag to ever be compared against.
int OBJ_PROCESS = 1;

struct KernelObject {
    bool used;
    int type;
    int dataIndex;   // meaning depends on type - for OBJ_PROCESS, an index into gProcesses[]
}

KernelObject gObjects[8];
int gObjectCount;

struct Handle {
    bool used;
    int objectIndex;
}

int HANDLES_PER_PROCESS = 8;

// MiniC has no 2D array declarations - flattened manually, same as any
// C codebase without them: process P's handle H lives at
// gHandleTables[P * HANDLES_PER_PROCESS + H].
Handle gHandleTables[32];   // 4 processes * 8 handles each

int allocObject(int type, int dataIndex) {
    int i = 0;
    while (i < 8) {
        if (!gObjects[i].used) {
            gObjects[i].used = true;
            gObjects[i].type = type;
            gObjects[i].dataIndex = dataIndex;
            gObjectCount = gObjectCount + 1;
            return i;
        }
        i = i + 1;
    }
    return -1;
}

// Every process's handle table starts empty, so the FIRST handle ever
// allocated into it always lands in slot 0 - spawnProcess() relies on
// this to give every process a well-known "handle 0 = myself" without
// needing a dedicated syscall just to look it up.
int allocHandle(int processIndex, int objectIndex) {
    int i = 0;
    while (i < HANDLES_PER_PROCESS) {
        int slot = processIndex * HANDLES_PER_PROCESS + i;
        if (!gHandleTables[slot].used) {
            gHandleTables[slot].used = true;
            gHandleTables[slot].objectIndex = objectIndex;
            return i;
        }
        i = i + 1;
    }
    return -1;
}

// Resolves a handle within one specific process's OWN table to the
// underlying object's index, or -1 if it's out of range or was never
// allocated. `handle` is the one value in this whole path that's
// attacker/ring3-controlled (a syscall argument) - bounds-checking it
// here, not trusting it as a bare array index, is the actual point of
// having a handle table at all rather than exposing gObjects[] directly.
int resolveHandle(int processIndex, int handle) {
    if (handle < 0 || handle >= HANDLES_PER_PROCESS) {
        return -1;
    }
    int slot = processIndex * HANDLES_PER_PROCESS + handle;
    if (!gHandleTables[slot].used) {
        return -1;
    }
    return gHandleTables[slot].objectIndex;
}
