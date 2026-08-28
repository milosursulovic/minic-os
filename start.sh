#!/usr/bin/env bash
# Builds (if needed) and boots minic-os with a real GTK display, so the
# host's actual mouse/keyboard reach the guest's PS/2 drivers - unlike
# `build.sh run`'s curses display, which relays keyboard only (no mouse).
#
# Usage: ./start.sh

set -e
cd "$(dirname "$0")"

bash build.sh
if [ ! -f disk.img ]; then
    make disk
fi

qemu-system-x86_64 -kernel kernel.elf -display gtk -drive file=disk.img,format=raw,if=ide
