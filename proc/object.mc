// Milestone 14: a generic kernel object model + per-process handle
// tables - the NT-style piece of the long-term architecture plan.
// Deliberately not introduced any earlier: a handle only means something
// once there's a real user/kernel boundary worth protecting (milestone
// 11) and real per-process state worth wrapping (milestones 12/13) -
// before that, everything was ring 0 and saw everything anyway, so a
// handle indirection would have been pure ceremony.
//
// Two tables, matching the real NT design this is modeled on: a single
// kernel-wide object table (g_objects) holding the actual objects, and a
// SEPARATE table per process (g_handle_tables) of small integers that
// index into it. Ring3 code only ever sees the small integer - it can
// never walk straight into g_objects[] itself; every syscall that
// resolves a handle (syscall.mc's num 3/7/8) does its own inline
// bounds/existence/rights check directly against g_handle_tables rather
// than through a shared helper, since milestone 27 made every one of
// those checks also need the handle's `rights` field, not just its
// object_index - a single "resolve to object index" helper stopped being
// the natural shared shape once rights entered the picture.

// A real `const` as of the `minic` compiler's const support (added the
// same session this needed it) - was a plain, only-by-convention-const
// global before that, same as everything else in this codebase still
// not yet converted (g_heap_base, etc.). No OBJ_NONE constant - an
// unallocated KernelObject's zero-valued `type` field is never read
// (guarded by `used` everywhere), so there's nothing for a "none" tag
// to ever be compared against.
const int obj_process = 1;
// Milestone 25: the first object type actually wrapped by real,
// enforced per-handle rights (see RIGHT_* below) rather than "any
// handle to this object can do anything the object supports."
const int obj_channel = 2;

struct kernel_object {
    bool used;
    int type;
    int data_index;   // meaning depends on type - for OBJ_PROCESS, an index into
                      // g_processes[]; for OBJ_CHANNEL, an index into g_channels[]
}

kernel_object g_objects[8];
int g_object_count;

// Milestone 25: real per-handle rights, the actual "capability" in
// "capability/permission system" - a bitmask of what operations THIS
// handle specifically permits, checked at the syscall boundary before
// the underlying object is touched at all. Before this, a handle only
// answered "does this integer refer to anything real" (bounds/existence
// checked since milestone 14) - never "is THIS process allowed to do
// THIS operation with it." A single bit space is shared across object
// types on purpose (simpler than a per-type rights enum) - safe in
// practice since a handle's type is always checked first anyway (a
// process handle's bits are never interpreted as channel rights).
const int right_query = 1;      // e.g. syscall 3's handle-query, on an OBJ_PROCESS handle
const int right_send = 2;       // Channel.send(), on an OBJ_CHANNEL handle
const int right_receive = 4;    // Channel.receive(), on an OBJ_CHANNEL handle

struct handle {
    bool used;
    int object_index;
    int rights;
}

const int handles_per_process = 8;

// A genuine 2D array as of the `minic` compiler's multi-dimensional-
// array support (added the same session this needed it) - was a
// manually flattened `Handle g_handle_tables[32]` before that, with
// `g_handle_tables[process_index * HANDLES_PER_PROCESS + handle]` standing
// in for `g_handle_tables[process_index][handle]`. Removed the workaround
// once the real feature existed, same as this project always has.
handle g_handle_tables[4][8];   // 4 processes * 8 handles each

int alloc_object(int type, int data_index) {
    int i = 0;
    while (i < 8) {
        if (!g_objects[i].used) {
            g_objects[i].used = true;
            g_objects[i].type = type;
            g_objects[i].data_index = data_index;
            g_object_count = g_object_count + 1;
            return i;
        }
        i = i + 1;
    }
    return -1;
}

// Every process's handle table starts empty, so the FIRST handle ever
// allocated into it always lands in slot 0 - spawn_process() relies on
// this to give every process a well-known "handle 0 = myself" without
// needing a dedicated syscall just to look it up. `rights` (milestone
// 25) is set once, here, at grant time - never widened later, so
// whatever policy the granting code chooses (see syscall.mc's num 9)
// is exactly what the handle can ever do for its whole lifetime.
int alloc_handle(int process_index, int object_index, int rights) {
    int i = 0;
    while (i < handles_per_process) {
        if (!g_handle_tables[process_index][i].used) {
            g_handle_tables[process_index][i].used = true;
            g_handle_tables[process_index][i].object_index = object_index;
            g_handle_tables[process_index][i].rights = rights;
            return i;
        }
        i = i + 1;
    }
    return -1;
}
