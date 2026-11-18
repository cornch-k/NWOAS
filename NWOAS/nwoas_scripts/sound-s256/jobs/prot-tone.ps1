# SPDX-License-Identifier: MIT
# NWOAS S259 (rev e: Live subkey, queue-level counters, event timeline, RX stall snapshot): staged hardware test of the NWAU0001 speaker thermal protection.
# Sets the default render endpoint level (dB, unmuted), plays a sine generated in memory
# (48 kHz 16-bit stereo, 20 ms fades) asynchronously, and prints the driver's Device
# Parameters once per second (the driver snapshots them every 1 s while playing).
# Usage (from a job .cmd, as SYSTEM):
#   powershell -NoProfile -ExecutionPolicy Bypass -File prot-tone.ps1 -Dbfs -10 -Seconds 20 -EndpointDb 0
# Output is bounded (one line per second + header/footer).  Not run by the agent that wrote it.
param([double]$Dbfs = -10, [int]$Seconds = 20, [double]$EndpointDb = 0, [double]$Freq = 1000)
$ErrorActionPreference = 'Stop'
$kd = 'HKLM:\SYSTEM\CurrentControlSet\Enum\ACPI\NWAU0001\0\Device Parameters'
$k = $kd + '\Live'     # volatile copy the driver refreshes every second while playing (no disk flush)
$names = 'NwAudioState','NwAudioProtState','NwAudioProtTripReason','NwAudioProtTrips','NwAudioProtVerified',
         'NwAudioGainReg','NwAudioVolReg','NwAudioProtFloor','NwAudioCoilTempMilliC','NwAudioMagnetTempMilliC',
         'NwAudioSensePowerUw','NwAudioSenseVrmsMv','NwAudioSenseIrmsMa','NwAudioSenseZMilliohm',
         'NwAudioSenseRMilliohm','NwAudioSenseRatioPermille','NwAudioSenseSat','NwAudioRxDescCompleted',
         'NwAudioPadChunks','NwAudioMaxRealGapUs','NwAudioMaxStolenUs','NwAudioRealGaps40ms','NwAudioRxRecoveries',
         'NwAudioMinQueueUs','NwAudioMaxServiceGapUs'
$final = 'NwAudioSense','NwAudioStartStatus','NwAudioLastRunStatus','NwAudioLastStopReason','NwAudioProtRaises',
         'NwAudioProtDrops','NwAudioMaxCoilTempMilliC','NwAudioSenseRMinMilliohm','NwAudioSenseRTempMilliC',
         'NwAudioSenseExpMv2','NwAudioSenseBlocks','NwAudioSenseBadStale','NwAudioSenseBadUnwritten',
         'NwAudioSenseBadSaturated','NwAudioSenseBadNegPower','NwAudioSenseBadImpedance','NwAudioSenseBadDead',
         'NwAudioSenseBadOvertemp','NwAudioRxDescSubmitted','NwAudioRxStrayReports','NwAudioRxReportFaults',
         'NwAudioRxRingErrors','NwAudioRxOfUf','NwAudioRxSetupFails','NwAudioSenseCfgFails','NwAudioAmpTdm4',
         'NwAudioTxCarveout','NwAudioRxCarveout','NwAudioRxConf','NwAudioThermRestored','NwAudioAmpState',
         'NwAudioI2cErrors','NwAudioAmpVerifyFails','NwAudioDmaRanDry','NwAudioOsStarved',
         'NwAudioMaxServiceGapUs','NwAudioRealGaps80ms','NwAudioMaxGapRenderUs','NwAudioMaxGapTimerUs',
         'NwAudioMaxServiceUs','NwAudioMaxSenseUs','NwAudioI2cOps','NwAudioI2cMaxUs','NwAudioI2cTotalMs',
         'NwAudioAmpWorkRuns','NwAudioMaxAmpWorkUs','NwAudioVolSets','NwAudioGateRefusals','NwAudioBlindMs',
         'NwAudioStreamRealMs','NwAudioStreamGuestMs','NwAudioOfUf','NwAudioDescSubmitted','NwAudioDescCompleted',
         'NwAudioPadChunks','NwAudioPadBytes','NwAudioDropBytes','NwAudioMinQueueUs','NwAudioMinInflight',
         'NwAudioDryRecoveries','NwAudioPushBytes','NwAudioProtBridges','NwAudioRxQuietRestarts',
         'NwAudioRxSramSize','NwAudioRxCarveoutSet','NwAudioRxStallDesc','NwAudioRxStallReport','NwAudioRxStallStatus',
         'NwAudioRxStallResidue','NwAudioRxStallSerdes','NwAudioRxStallMcaInt','NwAudioRxStallInflight'
function Snap($list) {
    if (Test-Path $k) { $i = Get-ItemProperty $k } else { $i = Get-ItemProperty $kd }
    ($list | ForEach-Object { $_.Substring(7) + '=' + ([int64][uint32]$i.$_) }) -join ' '
}

Add-Type -TypeDefinition @'
using System;
using System.Runtime.InteropServices;
namespace NwoasVol {
  [Guid("5CDF2C82-841E-4546-9722-0CF74078229A"), InterfaceType(ComInterfaceType.InterfaceIsIUnknown)]
  public interface IAudioEndpointVolume {
    int RegisterControlChangeNotify(IntPtr p);
    int UnregisterControlChangeNotify(IntPtr p);
    int GetChannelCount(out uint n);
    int SetMasterVolumeLevel(float db, ref Guid ctx);
    int SetMasterVolumeLevelScalar(float v, ref Guid ctx);
    int GetMasterVolumeLevel(out float db);
    int GetMasterVolumeLevelScalar(out float v);
    int SetChannelVolumeLevel(uint ch, float db, ref Guid ctx);
    int SetChannelVolumeLevelScalar(uint ch, float v, ref Guid ctx);
    int GetChannelVolumeLevel(uint ch, out float db);
    int GetChannelVolumeLevelScalar(uint ch, out float v);
    int SetMute([MarshalAs(UnmanagedType.Bool)] bool mute, ref Guid ctx);
    int GetMute([MarshalAs(UnmanagedType.Bool)] out bool mute);
    int GetVolumeStepInfo(out uint step, out uint count);
    int VolumeStepUp(ref Guid ctx);
    int VolumeStepDown(ref Guid ctx);
    int QueryHardwareSupport(out uint mask);
    int GetVolumeRange(out float minDb, out float maxDb, out float incDb);
  }
  [Guid("D666063F-1587-4E43-81F1-B948E807363F"), InterfaceType(ComInterfaceType.InterfaceIsIUnknown)]
  public interface IMMDevice {
    int Activate(ref Guid iid, int ctx, IntPtr p, [MarshalAs(UnmanagedType.IUnknown)] out object o);
  }
  [Guid("A95664D2-9614-4F35-A746-DE8DB63617E6"), InterfaceType(ComInterfaceType.InterfaceIsIUnknown)]
  public interface IMMDeviceEnumerator {
    int EnumAudioEndpoints(int flow, int mask, out IntPtr devs);
    int GetDefaultAudioEndpoint(int flow, int role, out IMMDevice dev);
  }
  [ComImport, Guid("BCDE0395-E52F-467C-8E3D-C4579291692E")] public class MMDeviceEnumerator { }
  public static class Ep {
    public static IAudioEndpointVolume Get() {
      var e = (IMMDeviceEnumerator)(new MMDeviceEnumerator());
      IMMDevice d; Marshal.ThrowExceptionForHR(e.GetDefaultAudioEndpoint(0, 1, out d));
      Guid iid = typeof(IAudioEndpointVolume).GUID; object o;
      Marshal.ThrowExceptionForHR(d.Activate(ref iid, 23, IntPtr.Zero, out o));
      return (IAudioEndpointVolume)o;
    }
    public static string Set(float db) {
      var v = Get(); Guid g = Guid.Empty; float before, after, mn, mx, inc; bool m;
      v.GetMasterVolumeLevel(out before); v.GetVolumeRange(out mn, out mx, out inc);
      if (db > mx) db = mx;
      if (db < mn) db = mn;
      Marshal.ThrowExceptionForHR(v.SetMasterVolumeLevel(db, ref g));
      Marshal.ThrowExceptionForHR(v.SetMute(false, ref g));
      v.GetMasterVolumeLevel(out after); v.GetMute(out m);
      return "ENDPOINT before=" + before + " dB after=" + after + " dB range=" + mn + ".." + mx + " mute=" + m;
    }
  }
}
'@

"PROT-TONE freq=$Freq Hz level=$Dbfs dBFS seconds=$Seconds endpoint=$EndpointDb dB"
try { [NwoasVol.Ep]::Set([float]$EndpointDb) } catch { 'ENDPOINT FAIL ' + $_.Exception.Message }
'BEFORE ' + (Snap $names)

$sr = 48000; $n = [int]($Seconds * $sr); $f = [int](0.02 * $sr)
$a = 32767 * [math]::Pow(10, $Dbfs / 20)
$ms = New-Object IO.MemoryStream; $bw = New-Object IO.BinaryWriter $ms
$bw.Write([Text.Encoding]::ASCII.GetBytes('RIFF')); $bw.Write([int](36 + $n * 4))
$bw.Write([Text.Encoding]::ASCII.GetBytes('WAVEfmt ')); $bw.Write([int]16); $bw.Write([int16]1); $bw.Write([int16]2)
$bw.Write([int]$sr); $bw.Write([int]($sr * 4)); $bw.Write([int16]4); $bw.Write([int16]16)
$bw.Write([Text.Encoding]::ASCII.GetBytes('data')); $bw.Write([int]($n * 4))
$w = 2 * [math]::PI * $Freq / $sr
for ($j = 0; $j -lt $n; $j++) {
    $g = [math]::Min(1.0, [math]::Min($j / $f, ($n - 1 - $j) / $f))
    $s = [int16][math]::Round($a * $g * [math]::Sin($w * $j))
    $bw.Write($s); $bw.Write($s)
}
$bw.Flush(); $ms.Position = 0
$p = New-Object Media.SoundPlayer $ms
$p.Load()
$t0 = Get-Date
$p.Play()
for ($t = 1; $t -le $Seconds + 1; $t++) {
    Start-Sleep -Seconds 1
    ('T+{0,3}s ' -f $t) + (Snap $names)
}
$p.Stop()
'PLAY END ' + [int]((Get-Date) - $t0).TotalMilliseconds + ' ms'
Start-Sleep -Seconds 3
'AFTER  ' + (Snap $names)
'FINAL  ' + (Snap $final)
# event timeline (driver ring, ADMAC-clock ms since the stream's StreamRun; guest ms in brackets)
$tn = @{1='START';2='STOP';3='PAD';4='DROP';5='DRY';6='DRYREC';7='GAIN';8='VOL';9='TRIP';10='VERIFIED';11='RXREC';12='STALL';13='FADE';14='BRIDGE';15='RXQUIET'}
$trip = @{1='STALE';2='UNWRITTEN';3='SATURATED';4='NEG_POWER';5='IMPEDANCE';6='DEAD';7='OVERTEMP';8='SETUP'}
try {
    if (Test-Path $k) { $ev = (Get-ItemProperty $k).NwAudioEvents } else { $ev = (Get-ItemProperty $kd).NwAudioEvents }
    if ($ev -and $ev.Length -ge 8) {
        $cnt = [BitConverter]::ToUInt32($ev, 0); $cap = [int](($ev.Length - 8) / 16)
        $first = [Math]::Max([int64]0, [int64]$cnt - $cap)
        'EVENTS total=' + $cnt + ' shown=' + ([int64]$cnt - $first)
        for ($i = $first; $i -lt $cnt; $i++) {
            $o = 8 + 16 * [int]($i % $cap)
            $rt = [BitConverter]::ToUInt32($ev, $o); $gt = [BitConverter]::ToUInt32($ev, $o + 4)
            $ty = [BitConverter]::ToUInt16($ev, $o + 8); $a = [BitConverter]::ToUInt16($ev, $o + 10)
            $b = [BitConverter]::ToUInt32($ev, $o + 12)
            $name = $tn[[int]$ty]; if (-not $name) { $name = 'T' + $ty }
            switch ($ty) {
                7  { $d = 'gain 0x{0:x2} -> 0x{1:x2}' -f $a, $b }
                8  { $d = 'vol {0} -> {1} ({2} writes)' -f $a, ($b -band 0xffff), ($b -shr 16) }
                9  { $d = 'reason ' + $trip[[int]$b] }
                12 { $d = 'gap guest {0} ms real {1} ms' -f $a, $b }
                14 { $d = 'sense gap {0} ms bridged (ceiling kept)' -f $b }
                15 { $d = 'RX restart: desc ring 0x{0:x4}{1:x4} report ring 0x{2:x4}' -f $a, ($b -band 0xffff), ($b -shr 16) }
                13 { if ($a -eq 2) { $d = 'fade-in done at vol ' + $b } else { $d = 'fade-out to vol ' + $b } }
                default { $d = 'a=' + $a + ' b=' + $b }
            }
            ('EVT {0,7} ms [{1,7}] {2,-8} {3}' -f $rt, $gt, $name, $d)
        }
    } else { 'EVENTS none' }
} catch { 'EVENTS FAIL ' + $_.Exception.Message }
