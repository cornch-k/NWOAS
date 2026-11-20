@echo off
echo GPUCOV K58 PSFILE (probe18 already delivered, no driver change, no restart): does a conditional select with a negated or absolute float operand (movc r, cond, -a, a) come out wrong on the AGX driver. Each case on AGX and on WARP, compared.
set D=C:\NWOAS\gpucov
if not exist %D%\nwgpuprobe18-arm64.exe (echo K58 probe18 missing & echo GPUCOV K58 DONE & exit /b 0)
set F=%D%\k58-cases.txt
> %F% echo sel_plain=(y ^< 0) ? 0.25 : 0.75
>> %F% echo sel_neg_same=(y ^< 0) ? -(abs(x)/200)+0.5 : (abs(x)/200)+0.5
>> %F% echo movc_neg=((y ^< 0) ? -(abs(x)/200) : (abs(x)/200))+0.5
>> %F% echo movc_neg2=((x*y ^< 0) ? -(abs(y)/200) : (abs(y)/200))+0.5
>> %F% echo movc_abs=(y ^< 0) ? abs(x/100) : (x/200+0.5)
>> %F% echo movc_const=((y ^< 0) ? -0.3 : 0.3)+0.5
>> %F% echo mul_sign=(abs(x)/200)*((y ^< 0) ? -1.0 : 1.0)+0.5
>> %F% echo atan_abs=atan(abs(y/x))/3.14159265+0.5
>> %F% echo atan_negabs=atan(-abs(y/x))/3.14159265+0.5
>> %F% echo atan_signed=atan(y/x)/3.14159265+0.5
%D%\nwgpuprobe18-arm64.exe psfile=%F% > %D%\k58-arm64.txt 2>&1
%D%\nwgpuprobe18-x64.exe psfile=%F% > %D%\k58-x64.txt 2>&1
powershell -NoProfile -Command "foreach($n in 'arm64','x64'){ $l=@(Get-Content ('C:\NWOAS\gpucov\k58-'+$n+'.txt') -ErrorAction SilentlyContinue); $l | Where-Object { $_ -match 'psfile' } | Select-Object -First 200 | ForEach-Object { 'K58 '+$n+' '+$_ }; 'K58 '+$n+' SUMMARY pass='+@($l -match 'psfile case=.*result=PASS').Count+' fail='+@($l -match 'psfile case=.*result=FAIL').Count }"
echo GPUCOV K58 DONE
exit /b 0
