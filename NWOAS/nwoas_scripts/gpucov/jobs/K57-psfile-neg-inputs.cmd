@echo off
echo GPUCOV K57 PSFILE (probe18, no driver change): the steps of the conic-gradient angle (atan2 of negated pixel-shader inputs), each drawn on the AGX driver and on WARP in one process and compared. x, y = the noperspective input (range -100..100)
set D=C:\NWOAS\gpucov
if not exist %D%\nwgpuprobe18-arm64.exe (echo K57 probe18 missing - deliver gpucov22 first & echo GPUCOV K57 DONE & exit /b 0)
set F=%D%\k57-cases.txt
> %F% echo pass_x=x/200+0.5
>> %F% echo neg_x=-x/200+0.5
>> %F% echo abs_x=abs(x)/100
>> %F% echo max_abs=max(abs(x),abs(y))/100
>> %F% echo min_abs=min(abs(x),abs(y))/100
>> %F% echo ratio=min(abs(x),abs(y))/max(abs(x),abs(y))
>> %F% echo lt_abs=(abs(x) ^< abs(y)) ? 0.75 : 0.25
>> %F% echo lt_negx_x=(-x ^< x) ? 0.75 : 0.25
>> %F% echo min_neg=min(-x,-y)/200+0.5
>> %F% echo max_neg=max(-x,-y)/200+0.5
>> %F% echo min_neg_lt=(min(-x,-y) ^< -min(-x,-y)) ? 0.75 : 0.25
>> %F% echo max_neg_ge=(max(-x,-y) ^>= -max(-x,-y)) ? 0.75 : 0.25
>> %F% echo both_sel=((min(-x,-y) ^< -min(-x,-y)) ^&^& (max(-x,-y) ^>= -max(-x,-y))) ? 0.75 : 0.25
>> %F% echo atan2_pos=atan2(y,x)/6.2831853+0.5
>> %F% echo atan2_neg=atan2(-y,-x)/6.2831853+0.5
>> %F% echo atan2_neg_tmp=atan2(-(y+p.y*0.0001),-(x+p.x*0.0001))/6.2831853+0.5
>> %F% echo atan2_t0=atan2(-t0.y,-t0.x)/6.2831853+0.5
>> %F% echo atan_ratio=atan(y/x)/3.14159265+0.5
%D%\nwgpuprobe18-arm64.exe psfile=%F% > %D%\k57-arm64.txt 2>&1
powershell -NoProfile -Command "$l=@(Get-Content C:\NWOAS\gpucov\k57-arm64.txt -ErrorAction SilentlyContinue); $l | Where-Object { $_ -match 'psfile' } | Select-Object -First 300 | ForEach-Object { 'K57 '+$_ }; 'K57 SUMMARY pass='+@($l -match 'psfile case=.*result=PASS').Count+' fail='+@($l -match 'psfile case=.*result=FAIL').Count"
echo GPUCOV K57 DONE
exit /b 0
