Option Explicit
On Error Resume Next
Dim svc, processes, item
Set svc = GetObject("winmgmts:\\.\root\cimv2")
If Err.Number <> 0 Then
  WScript.Echo "STOP: cannot check whether Setup is already running. Use the existing Setup window."
  WScript.Quit 2
End If
Set processes = svc.ExecQuery("SELECT Name FROM Win32_Process WHERE Name='setup.exe' OR Name='setuphost.exe' OR Name='setupprep.exe'")
If Err.Number <> 0 Then WScript.Quit 2
For Each item In processes
  WScript.Echo "STOP: Setup is already running. Values were applied; use Back/Next in that window."
  WScript.Quit 1
Next
If Err.Number <> 0 Then WScript.Quit 2
WScript.Quit 0
