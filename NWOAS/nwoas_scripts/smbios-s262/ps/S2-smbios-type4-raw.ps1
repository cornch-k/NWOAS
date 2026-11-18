# S262 S2: decode the raw SMBIOS table Windows received (types 0,1,4,7,17). Read-only.
$d=(Get-CimInstance -Namespace root\wmi -ClassName MSSmBios_RawSMBiosTables).SMBiosData
$v=Get-CimInstance -Namespace root\wmi -ClassName MSSmBios_RawSMBiosTables
'RAW bytes='+$d.Length+' version='+$v.SmbiosMajorVersion+'.'+$v.SmbiosMinorVersion
$i=0
while($i+4 -le $d.Length){
  $t=$d[$i]; $l=$d[$i+1]; $hd=[BitConverter]::ToUInt16($d,$i+2); if($t -eq 127 -or $l -lt 4){break}
  $j=$i+$l; $s=@()
  if($d[$j] -eq 0){ $j+=2 } else { while($d[$j] -ne 0){ $k=$j; while($d[$k] -ne 0){$k++}; $s+=[Text.Encoding]::ASCII.GetString($d,$j,$k-$j); $j=$k+1 }; $j++ }
  function S($n){ if($n -gt 0 -and $n -le $s.Count){'['+$s[$n-1]+']'}else{'-'} }
  function W($o){ [BitConverter]::ToUInt16($d,$i+$o) }
  if($t -eq 4){
    'T4 handle='+$hd+' len='+$l+' socket='+(S $d[$i+4])+' family='+$d[$i+6]+' manuf='+(S $d[$i+7])+' version='+(S $d[$i+0x10])
    'T4 extclk='+(W 0x12)+' max='+(W 0x14)+' current='+(W 0x16)+' status=0x'+('{0:x2}' -f $d[$i+0x18])+' L1h='+(W 0x1a)+' L2h='+(W 0x1c)+' L3h='+(W 0x1e)
    if($l -ge 0x28){ 'T4 part='+(S $d[$i+0x22])+' cores='+$d[$i+0x23]+' enabled='+$d[$i+0x24]+' threads='+$d[$i+0x25]+' char=0x'+('{0:x4}' -f (W 0x26)) }
    if($l -ge 0x30){ 'T4 family2='+(W 0x28)+' cores2='+(W 0x2a)+' enabled2='+(W 0x2c)+' threads2='+(W 0x2e) }
    if($l -ge 0x32){ 'T4 threadEnabled='+(W 0x30) }
    'T4 hex='+(($d[$i..($i+$l-1)] | ForEach-Object { '{0:x2}' -f $_ }) -join '')
  } elseif($t -eq 7){
    'T7 handle='+$hd+' name='+(S $d[$i+4])+' cfg=0x'+('{0:x4}' -f (W 5))+' max=0x'+('{0:x4}' -f (W 7))+' inst=0x'+('{0:x4}' -f (W 9))
  } elseif($t -eq 17){
    'T17 handle='+$hd+' sizeMB='+(W 0x0c)+' type='+$d[$i+0x12]+' speed='+(W 0x15)+' manuf='+(S $d[$i+0x17])+' part='+(S $d[$i+0x1a])
  } elseif($t -le 1){
    'T'+$t+' handle='+$hd+' strings='+(($s | ForEach-Object { '['+$_+']' }) -join ' ')
  }
  $i=$j
}
