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

#pragma GCC visibility pop
