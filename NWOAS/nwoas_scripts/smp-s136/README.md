# S136 vGIC interrupt-mask candidate

S136 changes one existing, default-off gate in the S133 hypervisor. A guest
write to `GICD_ICENABLER` means “disable this SPI”; the historical path called
`aic_set_mask(irq, false)`, which unmasks the Apple AIC interrupt. S136 calls
`aic_set_mask(irq, true)` for SPIs instead.

AppleWOA's fetched `origin/bugfix/vgic_fixes` branch independently contains the
same polarity correction. Keep S133 as the stable control. Use S136 only if
S133 visually freezes or a device IRQ keeps firing after Windows disables it,
then compare boot completion, input responsiveness, and fatal markers.
