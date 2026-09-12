# Hardware info collector for minic-os real-hardware bring-up debugging.
# Read-only - queries WMI/CIM, no changes made to the system.
# Run in PowerShell (Windows), then paste the console output back, or send
# the saved .txt file.
#
# If Windows blocks running the script, either:
#   powershell -ExecutionPolicy Bypass -File hw-info.ps1
# or right-click the file -> Properties -> Unblock -> OK, then run normally.

$out = "$env:USERPROFILE\Desktop\minic-os-hw-info.txt"
"" | Out-File -FilePath $out -Encoding utf8

function Add-Section($title, $block) {
    $line = "=" * 70
    "$line`n$title`n$line" | Tee-Object -FilePath $out -Append
    try {
        & $block | Out-String | Tee-Object -FilePath $out -Append
    } catch {
        "  (error collecting this section: $($_.Exception.Message))" | Tee-Object -FilePath $out -Append
    }
    "" | Tee-Object -FilePath $out -Append
}

Add-Section "CPU" {
    Get-CimInstance Win32_Processor |
        Select-Object Name, Manufacturer, NumberOfCores, NumberOfLogicalProcessors, MaxClockSpeed, AddressWidth
}

Add-Section "Motherboard / BIOS" {
    Get-CimInstance Win32_BaseBoard | Select-Object Manufacturer, Product, Version
    Get-CimInstance Win32_BIOS | Select-Object Manufacturer, SMBIOSBIOSVersion, ReleaseDate
}

Add-Section "System (manufacturer/model/RAM/firmware type)" {
    Get-CimInstance Win32_ComputerSystem |
        Select-Object Manufacturer, Model, SystemType, TotalPhysicalMemory, PCSystemType
    [PSCustomObject]@{ FirmwareType = $env:firmware_type }
}

Add-Section "RAM modules" {
    Get-CimInstance Win32_PhysicalMemory |
        Select-Object Manufacturer, Capacity, Speed, ConfiguredClockSpeed, DeviceLocator
}

Add-Section "GPU / display adapter (most relevant for the VBE/graphics question)" {
    Get-CimInstance Win32_VideoController |
        Select-Object Name, AdapterCompatibility, DriverVersion, VideoModeDescription, `
                       CurrentHorizontalResolution, CurrentVerticalResolution, CurrentRefreshRate, `
                       AdapterRAM, PNPDeviceID
}

Add-Section "Secure Boot state (needs admin - fine if this errors)" {
    try { Confirm-SecureBootUEFI } catch { "not available (needs admin, or this is a Legacy/CSM-only machine)" }
}

Add-Section "Disk controller mode hints (AHCI/RAID/NVMe)" {
    Get-CimInstance Win32_DiskDrive | Select-Object Model, InterfaceType, Size
}

Write-Host "`nDone. Report saved to: $out"
Write-Host "Paste its contents back, or send the file."
