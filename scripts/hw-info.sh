#!/usr/bin/env bash
# Hardware info collector for minic-os real-hardware bring-up debugging.
# Read-only - queries the kernel/DMI tables, no changes made to the system.
# Run in a terminal (some sections need sudo for DMI access), then paste
# the console output back, or send the saved .txt file.
#
#   sudo bash hw-info.sh
#
# (Some sections - RAM module details, motherboard/BIOS, Secure Boot state -
# are only readable as root; running without sudo still collects everything
# else and marks those sections as skipped rather than failing outright.)

set -u

if [ -d "$HOME/Desktop" ]; then
    OUT="$HOME/Desktop/minic-os-hw-info.txt"
else
    OUT="$HOME/minic-os-hw-info.txt"
fi
: > "$OUT"

section() {
    local title="$1"
    local line
    line=$(printf '=%.0s' $(seq 1 70))
    { echo "$line"; echo "$title"; echo "$line"; } | tee -a "$OUT"
}

run() {
    # Runs "$@", tees output to $OUT, and prints a clean "not available"
    # line instead of a raw error if the command doesn't exist or fails -
    # same "don't let one missing tool wreck the report" tone as the
    # PowerShell counterpart's try/catch per section.
    if command -v "$1" >/dev/null 2>&1; then
        if ! "$@" 2>&1 | tee -a "$OUT"; then
            echo "  (command exited non-zero - output above may be partial)" | tee -a "$OUT"
        fi
    else
        echo "  ($1 not installed - skipped)" | tee -a "$OUT"
    fi
    echo "" | tee -a "$OUT"
}

need_root_note() {
    if [ "$(id -u)" -ne 0 ]; then
        echo "  (needs root for full detail - re-run with sudo for this section)" | tee -a "$OUT"
        echo "" | tee -a "$OUT"
    fi
}

section "CPU"
run lscpu

section "Motherboard / BIOS"
if [ "$(id -u)" -eq 0 ]; then
    run dmidecode -t baseboard
    run dmidecode -t bios
else
    need_root_note
fi

section "System (manufacturer/model/RAM/firmware type)"
if [ "$(id -u)" -eq 0 ]; then
    run dmidecode -t system
else
    need_root_note
fi
run free -h
if [ -d /sys/firmware/efi ]; then
    echo "FirmwareType: UEFI" | tee -a "$OUT"
else
    echo "FirmwareType: BIOS (Legacy/CSM - no /sys/firmware/efi)" | tee -a "$OUT"
fi
echo "" | tee -a "$OUT"

section "RAM modules"
if [ "$(id -u)" -eq 0 ]; then
    run dmidecode -t memory
else
    need_root_note
fi

section "GPU / display adapter (most relevant for the VBE/graphics question)"
if command -v lspci >/dev/null 2>&1; then
    lspci -k | grep -A3 -iE 'vga|3d|display' | tee -a "$OUT"
    echo "" | tee -a "$OUT"
else
    echo "  (lspci not installed - skipped)" | tee -a "$OUT"
    echo "" | tee -a "$OUT"
fi
run glxinfo -B

section "Secure Boot state"
if command -v mokutil >/dev/null 2>&1; then
    run mokutil --sb-state
elif [ -d /sys/firmware/efi ]; then
    SB_VAR=$(find /sys/firmware/efi/efivars -maxdepth 1 -iname 'SecureBoot-*' 2>/dev/null | head -1)
    if [ -n "$SB_VAR" ]; then
        # Byte 5 of this efivar is the actual 0/1 flag (first 4 bytes are
        # attribute flags) - the same convention every SecureBoot-state
        # reader uses.
        STATE=$(od -An -tu1 "$SB_VAR" 2>/dev/null | awk '{print $5}')
        if [ "$STATE" = "1" ]; then
            echo "Secure Boot: enabled" | tee -a "$OUT"
        else
            echo "Secure Boot: disabled" | tee -a "$OUT"
        fi
    else
        echo "  (UEFI present but SecureBoot efivar not found)" | tee -a "$OUT"
    fi
else
    echo "  (not available - Legacy/CSM-only machine, or mokutil not installed)" | tee -a "$OUT"
fi
echo "" | tee -a "$OUT"

section "Disk controller mode hints (AHCI/RAID/NVMe)"
run lsblk -d -o NAME,MODEL,TRAN,SIZE,ROTA
if command -v lspci >/dev/null 2>&1; then
    lspci -k | grep -A2 -iE 'sata|nvme|raid|ide' | tee -a "$OUT"
    echo "" | tee -a "$OUT"
fi

echo ""
echo "Done. Report saved to: $OUT"
echo "Paste its contents back, or send the file."
