#!/usr/bin/env bash
# Builds (if needed) and boots minic-os with a real GTK display, so the
# host's actual mouse/keyboard reach the guest's PS/2 drivers - unlike
# `build.sh run`'s curses display, which relays keyboard only (no mouse).
#
# Once the desktop shell activates VBE graphics mode (real display, not
# text mode), the shell's usual VGA-text prompt/echo/output is no longer
# the thing actually shown on screen - but every shell command mirrors
# its output to the serial port too (vga_print + serial_print, side by
# side, everywhere in shell/shell.c), so -serial stdio here means you
# can type into the GTK window (it has keyboard focus, unrelated to
# which display mode is active) and read the shell's responses in THIS
# terminal instead.
#
# Usage: ./start.sh

set -e
cd "$(dirname "$0")"

bash build.sh
if [ ! -f disk.img ]; then
    make disk
fi

qemu-system-x86_64 -kernel kernel.elf -display gtk -serial stdio \
    -drive file=disk.img,format=raw,if=ide
