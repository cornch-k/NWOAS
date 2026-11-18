/*
 * NWOAS Mac mini (J274) internal speaker - hardware access layer.
 *
 * One global device (the adapter is a singleton, see NewAdapterCommon).
 * The launcher (nwoas_scripts/sound-s256/audio_module.py) powers and
 * configures the blocks before Windows starts and leaves the amplifier in
 * software shutdown; this layer only:
 *   - maps the 5 ACPI memory resources and validates the window header,
 *   - feeds the ring buffer + ADMAC TX descriptors while a stream runs,
 *   - toggles the MCA TXA serdes and the amplifier power state,
 *   - writes the amplifier volume (never louder than the header cap in
 *     static mode, never below the thermal model floor in protected mode),
 *   - runs the V/I sense capture (header v2) and the speaker thermal model,
 *     and raises the amp gain to 21 dB only while that protection is healthy,
 *   - records header / counters under the device "Device Parameters" key.
 */
#ifndef _NWAU_HW_H_
#define _NWAU_HW_H_

/* include after definitions.h (portcls.h / wdm.h) */
#include <portcls.h>

/* Stop reasons (NwAudioStats.last_stop_reason) */
#define NWAU_STOP_STREAM        1
#define NWAU_STOP_WATCHDOG      2
#define NWAU_STOP_OS_STARVED    3
#define NWAU_STOP_POWER         4
#define NWAU_STOP_SHUTDOWN      5
#define NWAU_STOP_AMP_VERIFY    6
#define NWAU_STOP_AMP_I2C       7
#define NWAU_STOP_RESTART       8
#define NWAU_STOP_THERMAL       9   /* thermal model above t_limit + t_headroom */

/* Default endpoint volume reported to Windows: -16 dB -> amp 0x50 with cap 0x30 */
#define NWAU_DEFAULT_LEVEL_16_16   (-16 * 65536)

_IRQL_requires_(PASSIVE_LEVEL)
NTSTATUS NwAuStart(_In_ PDEVICE_OBJECT Fdo, _In_ PRESOURCELIST ResourceList);

_IRQL_requires_(PASSIVE_LEVEL)
VOID NwAuShutdown(VOID);

_IRQL_requires_(PASSIVE_LEVEL)
VOID NwAuPowerDown(VOID);

_IRQL_requires_(PASSIVE_LEVEL)
VOID NwAuPowerUp(VOID);

_IRQL_requires_(PASSIVE_LEVEL)
VOID NwAuStreamRun(VOID);

_IRQL_requires_(PASSIVE_LEVEL)
VOID NwAuStreamPause(VOID);

_IRQL_requires_max_(DISPATCH_LEVEL)
VOID NwAuRenderPush(_In_reads_bytes_(Bytes) const BYTE *Pcm, _In_ ULONG Bytes);

_IRQL_requires_max_(DISPATCH_LEVEL)
VOID NwAuService(VOID);

/* hardware real time in 100 ns units (ADMAC 24 MHz counter, also counts while
   the hypervisor stops the guest); 0 when the device is not started */
_IRQL_requires_max_(DISPATCH_LEVEL)
ULONGLONG NwAuRealTime100ns(VOID);

_IRQL_requires_max_(DISPATCH_LEVEL)
VOID NwAuStreamStarved(VOID);

/* Channel: KS channel index (the amp is mono: the louder channel level is used) */
_IRQL_requires_(PASSIVE_LEVEL)
VOID NwAuSetVolumeLevel(_In_ ULONG Channel, _In_ LONG Level16_16);

_IRQL_requires_(PASSIVE_LEVEL)
VOID NwAuSetMute(_In_ ULONG Channel, _In_ BOOL Mute);

#endif /* _NWAU_HW_H_ */
