import sys
J='/Users/angigyeom/Desktop/DEV/NWOAS/nwoas_scripts/gpucov/jobs/'
def out_cmd(tag,x):
    return ("powershell -NoProfile -Command \"$x='%s'; $p='%s '+$x; $l=@(Get-Content ('C:\\NWOAS\\gpucov\\k43e-'+$x+'.txt') -ErrorAction SilentlyContinue); "
            "$p+' lines='+$l.Count; $l | Where-Object { $_ -match 'NWGP (begin|end|memladder2 (summary|rcpt (when|pool_demand|exec_failure|Wom1G4|Wom1G3Native|Wom1G3Unpub|ddi_status slot=(3[18]|63|4[67]|58) ))|d3d12 end)' } | "
            "Select-Object -First 70 | ForEach-Object { $p+' '+$_ }\"") % (x,tag)
def trace_cmd(tag,x):
    return ("powershell -NoProfile -Command \"$x='%s'; $p='%s '+$x; $t=@(Get-Content ('C:\\NWOAS\\gpucov\\k43e-'+$x+'.log') -ErrorAction SilentlyContinue); "
            "function Get-Cut($s){ $s.Substring(0,[math]::Min(260,$s.Length)) }; $p+' UMDTRACE lines='+$t.Count; "
            "$t | Where-Object { $_ -match ' hr=0x[89a-f][0-9a-f]{7} ' -and $_ -notmatch ' hr=0x8000000a ' } | ForEach-Object { $w=$_ -split ' '; $w[0]+' '+$w[1] } | Group-Object | Sort-Object Count -Descending | Select-Object -First 20 | ForEach-Object { $p+' UTHRG '+$_.Count+' '+$_.Name }; "
            "$a=@(); for($i=0;$i -lt $t.Count;$i++){ $s=$t[$i]; if(($s -match '^(reject-|runtime-set-error)') -or ($s -match '^measure-d3d12-(execute|owner-submit) hr=0x[89a-f]' -and $s -notmatch ' hr=0x8000000a ')){ if(($a.Count -eq 0) -or ($i -gt $a[-1]+60)){ $a+=$i } } }; "
            "$p+' ANCHORS count='+$a.Count+' at='+($a -join ','); foreach($i in ($a | Select-Object -First 2)){ for($k=[math]::Max(0,$i-12);$k -le [math]::Min($t.Count-1,$i+6);$k++){ $p+' UTCTX a'+$i+' '+$k+' '+(Get-Cut $t[$k]) } }; "
            "$t | Where-Object { $_ -match '^reject-' } | Select-Object -First 12 | ForEach-Object { $p+' UTFAIL '+(Get-Cut $_) }; $t | Where-Object { $_ -match '^measure-cpu-pool-rehome' } | Select-Object -First 8 | ForEach-Object { $p+' UTREHOME '+(Get-Cut $_) }\"") % (x,tag)
def job(tag,runs,desc,extra=''):
    s='@echo off\r\necho GPUCOV %s %s\r\n' % (tag,desc)
    for x in runs:
        s+=out_cmd(tag,x)+'\r\n'+trace_cmd(tag,x)+'\r\n'
    s+=extra
    s+='echo GPUCOV %s DONE\r\nexit /b 0\r\n' % tag
    return s
verdict=("powershell -NoProfile -Command \"function Get-MlResult($f,$m,$n){ $x=@(Get-Content ('C:\\NWOAS\\gpucov\\k43e-'+$f+'.txt') -ErrorAction SilentlyContinue) -match ('memladder2 d3d12 size_mib='+$m+' .*result=(PASS|FAIL)'); if($x.Count -gt $n -and ($x[$n] -match 'result=(PASS|FAIL)')){ $Matches[1] } else { 'NONE' } }; "
         "$a=Get-MlResult 'a' 2304 0; $b1=Get-MlResult 'b' 2048 0; $b2=Get-MlResult 'b' 2304 0; $d1=Get-MlResult 'd' 3072 0; $d2=Get-MlResult 'd' 2304 0; $c=@(); $cp=1; foreach($s in 1792,2048,2304,2560,3072){ $r=Get-MlResult 'c' $s 0; $c+=($s.ToString()+':'+$r); if($r -ne 'PASS'){ $cp=0 } }; $c2=Get-MlResult 'c' 2304 0; "
         "$v=if($a -eq 'FAIL' -and $c2 -eq 'FAIL'){'SIZE'}elseif($a -eq 'PASS' -and $c2 -eq 'PASS' -and ($b2 -eq 'FAIL' -or $d2 -eq 'FAIL')){'HISTORY'}elseif($a -eq 'PASS' -and $b2 -eq 'PASS' -and $d2 -eq 'PASS' -and $cp){'NOREPRO'}else{'MIXED'}; "
         "'K43e4 VERDICT a2304='+$a+' b2048='+$b1+' b2304='+$b2+' c='+($c -join ',')+' c_all_pass='+$cp+' d3072='+$d1+' d2304='+$d2+' => '+$v\"\r\n"
         "powershell -NoProfile -Command \"Get-WinEvent -FilterHashtable @{LogName='System'; StartTime=(Get-Date).AddMinutes(-20)} -ErrorAction SilentlyContinue | Where-Object { $_.ProviderName -match 'Display|dxgkrnl|DxgKrnl|Resource-Exhaustion' -or $_.Id -in 4101,2004 } | Select-Object -First 8 | ForEach-Object { 'K43e4 SYSEVT '+$_.TimeCreated.ToString('HH:mm:ss')+' '+$_.ProviderName+' '+$_.Id }\"\r\n"
         "if exist C:\\NWOAS\\gpucov\\nwgpuprobe13-arm64.exe (C:\\NWOAS\\gpucov\\nwgpuprobe13-arm64.exe rcpt > C:\\NWOAS\\gpucov\\k43e-rcpt.txt 2>&1)\r\n"
         "powershell -NoProfile -Command \"@(Get-Content C:\\NWOAS\\gpucov\\k43e-rcpt.txt -ErrorAction SilentlyContinue) | Where-Object { $_ -match 'rcpt (pool_demand|exec_failure|Wom1G4|Wom1G3Native|Wom1G3Unpub|Wom1GpuReset|Wom1G3BrokerFailures|Wom1G3PagingFailure|Wom1G3Poison|ddi_status)' } | Select-Object -First 48 | ForEach-Object { 'K43e4 now '+$_ }\"\r\n")
files={
 'K43e2-extract-ab-p13.cmd':job('K43e2','ab','EXTRACT (a) 2304 alone, (b) 2048 then 2304: probe lines, UMD trace failing stages, 12 lines before / 6 after each failure anchor'),
 'K43e3-extract-cd-p13.cmd':job('K43e3','cd','EXTRACT (c) one process per size 20 s apart, (d) 3072 then 2304: probe lines, UMD trace failing stages, anchors'),
 'K43e4-verdict-p13.cmd':job('K43e4','','VERDICT SIZE/HISTORY/NOREPRO/MIXED from k43e-a..d, display events, receipts now (probe13 rcpt)',verdict),
}
for n,s in files.items():
    open(J+n,'w',newline='').write(s)
for n in ['K43u-umdtrace-anchor-2774.cmd','K43e1-ladder-runs-p13.cmd']+list(files):
    s=open(J+n,newline='').read().replace('\r\n','\n').replace('\n','\r\n')
    s.encode('ascii')
    print(len(s.encode('ascii')),'%' in s.replace('%%','').replace('%D%','').replace('%P%','').replace('%TIME%','').replace('%T%',''),n)
