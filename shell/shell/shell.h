#pragma once

#pragma GCC visibility push(hidden)

void print_prompt(void);
void run_command(void);

// Command history, driven by kernel/isr/isr.c: shell_history_add() records
// a just-submitted line (called right after Enter), shell_history_up()/
// _down() are called on the Up/Down arrow extended scancodes to recall
// older/more-recent entries, redrawing the in-progress command line in
// place (real erase-then-retype, mirrored to serial/the GUI terminal the
// exact same way backspace already is).
void shell_history_add(const char* line);
void shell_history_up(void);
void shell_history_down(void);

// Tab-completion, driven by kernel/isr/isr.c on the Tab scancode (0x0F) -
// position-based, not command-aware: the first word completes against a
// fixed command-name table, anything after the first space completes
// against the current directory's own entries (fs_list_entry, same call
// cmd_ls already uses).
void shell_tab_complete(void);

#pragma GCC visibility pop
