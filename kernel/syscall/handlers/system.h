#pragma once
#include "../../../types.h"

#pragma GCC visibility push(hidden)

// Ticks/term-scrollback/sys-info/GUI-app-spawn/time/date - 35, 36, 40,
// 41, 42, 43.
bool syscall_system(u64 num, u64 a1, u64 a2, u64 a3, u64* result);
// Real shared GUI-app spawn path (app_id 0=Terminal/1=File Manager/
// 2=Settings/3=Device Manager/4=Service Manager) - used by syscall 41's
// own handler and by kernel/isr/isr.c's real Ctrl+Alt+T hotkey (Faza II
// point 17). Returns the new process index, or -1 on failure.
int spawn_gui_app_index(int app_id);

#pragma GCC visibility pop
