# S138 USB-A rescue boot

S138 keeps the established S130 one-core SSD/NVMe path and hides ACPI XHC1,
the SoC USB-C controller that reached `USBSTS=0x1d` and coincided with the
desktop freeze. The FL1100 USB-A controller remains visible so a keyboard can
start `NWOS.EXE`. This is a recovery/diagnostic configuration, not the final
driver configuration.
