# S164 CPU measurement preparation

PERCPU hardware baseline recorded on S163200us, five runs, all40 core samples
pass. No CPU frequency change has been applied. pstate12.py remains
HARDWARE-UNVERIFIED and is not loaded by any current launcher.

PERCPU.EXE runs the existing S140 25-million-iteration unsigned workload
sequentially on each of eight Windows logical processors, with affinity verified
before and after each measurement. It restores the prior affinity after each
sample and validates the known aggregate checksum. This is a small controlled
comparison, not Cinebench or a claim of macOS performance parity.

capture.py only reads T8103 command/status and existing throttle-control
registers before UEFI, after S140 init. It never changes DVFS or SMC ownership.
Selected/requested operating points are not calibrated delivered frequency.

Reference: Linux arch/arm64/boot/dts/apple/t8103.dtsi maps E5 to2064MHz,
P7 to1956MHz and P12 to2988MHz. P13..15 are disabled turbo entries. Source:
https://github.com/torvalds/linux/blob/master/arch/arm64/boot/dts/apple/t8103.dtsi
The Linux driver reads STATUS current[7:4] to select its OPP:
https://github.com/torvalds/linux/blob/master/drivers/cpufreq/apple-soc-cpufreq.c

## Review corrections

The raw Opus review overstates that all thermal controls are enabled by
cpufreq_init. Each feature follows its actual ADT pmgr property. The saved
Tahoe ADT reports cpu-apsc=1, ppt-thrtl=1, llc-thrtl=0, amx-thrtl=1,
cpu-fixed-freq-pll-relock=1. Preserve these existing controls, do not toggle
features as part of a pstate comparison. The combined T8103 PS1/PS2 mask is
0xf01f (5-bit PS1, 4-bit PS2), not the review's proposed0x1f01f.
Live readback is needed before any clock candidate. No SMC client is started.

## Deployment

The three upload-NN.cmd scripts write only C:\NWOAS-S164\PERCPU.B64.
upload-verify.cmd decodes to PERCPU.NEW, checks size and SHA256 before moving
to PERCPU.EXE. Queue each only after the previous job completes. measure.cmd
runs five samples. No USB movement or driver installation is required.


The opt-in pstate12.py module requires the S140 P7 baseline, preserves existing
feature controls, and attempts P7 restoration on transition timeout/readback
mismatch before aborting. Five fake-proxy tests run the actual module and pass.
These do not validate P12 hardware stability. Do not combine its first test
with a new storage, memory or USB candidate.


### Follow-up review and launch guard

The prepared P12 module now waits for BUSY to clear before any request,
including rollback. If BUSY remains set, it aborts without writing P7 into a
busy transition; do not report successful rollback in that case. A command
readback mismatch with an idle controller does attempt P7. Selected STATUS is
recorded by the module, and capture.py records APSC_BUSY separately.

Fable incorrectly suggested bit20 might be T8103 APSC_DIS. Actual cpufreq.c
explicitly defines M1_APSC_DIS as bit22 and UNK_M1 as bit20. The fake baseline
sets bit20 to check preservation; it does not redefine bit20 as APSC control.

run_guest.py previously catches module errors, enters an interactive shell,
and can start the guest if that shell exits. The new explicit --strict-init
option re-raises script/command exceptions before guest entry. Legacy
interactive behavior remains the default. Four tests exercise the actual AST
launcher tail for both modes. Candidate g50/query/USB-C launchers opt in;
the currently running S163200us process is unaffected.

Compare the same PERCPU binary at P7 and P12. The expected performance signal
is shorter P-core times with E-core times broadly unchanged, not a different
checksum or the Windows clock display. Linux OPP ratio1956/2988~0.655 is a
hypothesis, not an acceptance guarantee under background work/throttling.
Session log paths and manifests identify the boot/settings; the EXE does not.
