# S229 review disposition

F1 and F2 are fixed in S230. The tick-path arming printf is suppressed while
the C frontend owns the controller. SQ1 doorbells remain ignored after shutdown
completion even when the guest clears SHN. The actual control/MMIO harness now
asserts both paths. S229 is historical first-boot evidence, not the final image.

F3 identifies a real test boundary: the original adapter harness mocks the
physical process function. Hardware write/read and reboot evidence complements
that harness but is not physical fault injection. Additional extracted-function
coverage, if completed, is documented separately under S230.

The review sentence saying S228 callback reentry is unchanged is stale: the
reviewed composition already contains the control_busy guard documented in
S228 claude-resolution.md. No source change was needed for that observation.

Physical I/O is synchronous under bhl. A physical backend fault is terminal for
the controller until a fresh runtime boot; CC.EN reset does not clear it.
