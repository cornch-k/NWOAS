# USB-C lifecycle reference, 2026-09-10

Primary source: https://github.com/torvalds/linux/blob/master/drivers/usb/dwc3/dwc3-apple.c (Asahi Linux contributors, GPL-2.0); read upstream on2026-09-10. Related DTS: https://github.com/torvalds/linux/blob/master/arch/arm64/boot/dts/apple/t8103.dtsi .

Upstream describes coordinated Type-C mux/PHY/DWC3 start and stop ordering, USB2 host mode selection before PHY power-up, and USB3 setup after core initialization. It explicitly documents reconnect-event loss unless the PHY and external USB2 repeater are resynchronized on disconnect/reconnect. Consequently a transfer-ring alias correction cannot establish hotplug support on its own. Our D81/D83 harness performs a preboot setup and transfer address adjustments, not the full dynamic Type-C lifecycle. This is a separate missing capability, not proof that it caused every observed pointer freeze.

First validate a controller with the cable already present and use that result to separate DMA/ring problems from unplug/replug behavior. Full hotplug requires coordinating the non-debug port only; the debug/host link port must stay alive. A future native Windows driver needs equivalent lifecycle integration rather than an uncoordinated xHCI-only reset. No upstream code copied into the BSD/MIT firmware during this review.
