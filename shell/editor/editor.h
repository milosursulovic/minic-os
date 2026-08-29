#pragma once

#include "../../types.h"

#pragma GCC visibility push(hidden)

// Set while a full-screen `edit <name>` session owns the VGA console -
// kernel/isr/isr.c routes every keystroke to editor_handle_scancode()
// instead of the normal shell line-buffer path while this is true, and
// kmain.c's main loop skips its own post-command prompt reprint (editor_
// save_and_exit() prints the next prompt itself, once the screen is its
// own to draw on again).
extern bool g_editor_active;

// path is expected to already be resolved (join_path against the shell's
// cwd, same as every other fs command in shell.c) - editor.c does no
// path resolution of its own.
void editor_start(const char* path);

// Called by kernel/isr/isr.c's IRQ1 handler in place of its normal
// Backspace/char-table handling whenever g_editor_active is true.
void editor_handle_scancode(u8 scancode);

#pragma GCC visibility pop
