// Milestone 15: IPC channels between isolated processes. A channel is a
// single-slot "mailbox" holding one u64 message - deliberately the
// simplest thing that can prove real blocking message-passing, not a
// full ring buffer (that's a straightforward but separate extension,
// not needed to prove the one hard problem this milestone is actually
// about).
//
// The point isn't the mailbox itself - it's that sched/task.mc's
// channel_receive() blocks the calling task exactly the way sleep()
// already does (yield() wakes it via the same scan, just checking
// channel_has_message() instead of a wake tick), so IPC needed zero new
// scheduling machinery, only a second *kind* of wake condition layered
// onto the one that already existed. channel_receive() itself lives in
// task.mc, not here - it needs Task/g_current_task/yield(), the same
// reason sleep() lives there rather than in its own file. This file
// stays scheduler-agnostic on purpose: no import of task.mc, since a
// struct type (Task) referenced from a file that gets parsed before
// task.mc's own `struct Task` declaration is reached (import processing
// is eager/inline, not deferred) doesn't resolve - a real, sharp edge in
// this compiler's forward-reference handling, worth remembering if
// another cross-file struct dependency shows up again.
//
// send() is deliberately non-blocking: it fails outright if the mailbox
// is already full, rather than either overwriting an unread message or
// blocking the sender too. A blocking send is a second hard problem
// (symmetric blocking, sender/receiver both able to wait on each other)
// - not tackled this milestone, see README's Known limitations.

struct channel {
    bool used;
    bool full;
    u64 message;
}

channel g_channels[4];
int g_channel_count;

int create_channel() {
    if (g_channel_count >= 4) {
        return -1;
    }
    int idx = g_channel_count;
    g_channels[idx].used = true;
    g_channels[idx].full = false;
    g_channel_count = g_channel_count + 1;
    return idx;
}

// The wake condition sched/task.mc's yield() checks for a task blocked
// on a channel - true the instant a message is waiting.
bool channel_has_message(int channel_index) {
    return g_channels[channel_index].full;
}

bool channel_send(int channel_index, u64 value) {
    if (g_channels[channel_index].full) {
        return false;
    }
    g_channels[channel_index].message = value;
    g_channels[channel_index].full = true;
    return true;
}
