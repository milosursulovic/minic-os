#pragma once

#pragma GCC visibility push(hidden)

// Real VFS-absolute path ("" = the virtual root, "/system", "/system/sub",
// "/devices", "/processes", ...) - shared between shell.c's own
// shell_tab_complete() (path-argument completion) and commands/fs.c
// (which owns the definition and every cd/ls/mkdir/... command that
// reads or updates it).
extern char g_shell_cwd[128];

// Splits "<first> <second>" into two path fragments - shared by
// commands/fs.c's cp/mv and commands/service.c's cmd_service, the only
// commands here that take two arguments. Defined in shell.c since it's
// genuinely cross-domain. Returns false (usage error already printed) if
// there's no second argument.
bool split_two_args(char* args, char* first_out, char** second_out);

#pragma GCC visibility pop
