# S176 benchmark user-session launcher

Prepared, hardware untested. Existing NWOS starts as SYSTEM and synchronously waits for a command. A GUI application waiting in session0 can block that worker. The previous Cinebench command currently has no result; session0 waiting is a hypothesis, not established.

USERCB.EXE uses WTSGetActiveConsoleSessionId, WTSQueryUserToken and CreateProcessAsUserW to launch exactly C:\NWOAS-BENCH\run-user.cmd in the already logged-on console session. No password/token changes, privilege adjustments or new scheduled tasks. Fails if no logged-on user or launch rights. Token/environment/handles closed. No cross-session handle inheritance. Hidden new console, user environment, existing desktop. Caller returns immediately so diagnostic worker stays available.

CBRUN.CMD runs a CPU-only benchmark with a30min process timeout and progress on CPU time/working set/window title every30s. Outputs to user-writable Public Documents\NWOAS-BENCH; refuses previoussummarylog. This is diagnostic execution, not a silent/formal performance comparison. Benchmark/application UI may still create its own window on Mac mini; MacBook UI unaffected.

Build statically asserts Windows ABI layouts, ARM64/noCRT. APIs checked against Microsoft docs2026-09-10:
https://learn.microsoft.com/en-us/windows/win32/api/wtsapi32/nf-wtsapi32-wtsqueryusertoken
https://learn.microsoft.com/en-us/windows/win32/api/processthreadsapi/nf-processthreadsapi-createprocessasuserw

No successful launch or benchmark claimed yet. Prepared S172 same-payload launcher adds tools to host-RAM FAT only.
