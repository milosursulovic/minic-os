#!/usr/bin/env bash
# Thin wrapper around the real build system (Makefile) - kept so the
# documented command surface (./build.sh / ./build.sh run / ./build.sh
# iso / ./build.sh disk) doesn't change shape now that the kernel is C
# instead of MiniC, even though what runs underneath does.
#
# Usage: ./build.sh          # just build kernel.elf
#        ./build.sh run      # build, then boot it in QEMU (curses display)
#        ./build.sh iso      # build, then package a GRUB-bootable minic-os.iso
#        ./build.sh disk     # (re)build disk.img from scratch

set -e
cd "$(dirname "$0")"

case "$1" in
    run) make run ;;
    iso) make iso ;;
    disk) make disk ;;
    "") make ;;
    *) echo "usage: $0 [run|iso|disk]" >&2; exit 1 ;;
esac
