// Milestone 15: IPC channels between isolated processes. A channel is a
// single-slot "mailbox" holding one u64 message - deliberately the
// simplest thing that can prove real blocking message-passing, not a
// full ring buffer (that's a straightforward but separate extension,
// not needed to prove the one hard problem this milestone is actually
// about).
//
// The point isn't the mailbox itself - it's that sched/task.mc's
// channelReceive() blocks the calling task exactly the way sleep()
// already does (yield() wakes it via the same scan, just checking
// channelHasMessage() instead of a wake tick), so IPC needed zero new
// scheduling machinery, only a second *kind* of wake condition layered
// onto the one that already existed. channelReceive() itself lives in
// task.mc, not here - it needs Task/gCurrentTask/yield(), the same
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

struct Channel {
    bool used;
    bool full;
    u64 message;
}

Channel gChannels[4];
int gChannelCount;

int createChannel() {
    if (gChannelCount >= 4) {
        return -1;
    }
    int idx = gChannelCount;
    gChannels[idx].used = true;
    gChannels[idx].full = false;
    gChannelCount = gChannelCount + 1;
    return idx;
}

// The wake condition sched/task.mc's yield() checks for a task blocked
// on a channel - true the instant a message is waiting.
bool channelHasMessage(int channelIndex) {
    return gChannels[channelIndex].full;
}

bool channelSend(int channelIndex, u64 value) {
    if (gChannels[channelIndex].full) {
        return false;
    }
    gChannels[channelIndex].message = value;
    gChannels[channelIndex].full = true;
    return true;
}
