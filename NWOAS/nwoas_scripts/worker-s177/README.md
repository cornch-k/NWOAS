# S177 installed Windows worker: RAM-only command scratch

Main implementation; Opus4.8 API rejected the distinct earlier robustness request. No model fallback/reword retry. This candidate is a narrower fix for confirmed ERROR_DISK_FULL112 in C:\Windows\Temp\NWOAS123.CMD: keep CMD scripts on host RAM FAT partition. No timeout/termination logic was added.

Authenticate existing NS2 via original exact disk capacity, MBR signature and session challenge. Determine its Windows volume letter by IOCTL_VOLUME_GET_VOLUME_DISK_EXTENTS: exactly one extent on that authenticated physical disk number, offset1MiB,length32MiB, volume serial0x53313233. Reject missing/ambiguous matches. Write only <letter>:\NWOAS177.CMD. This is RAM scratch, not an independent native transport.

Child CMD gets inherited read-only NUL stdin and CREATE_NO_WINDOW, while stdout/stderr pipe and completion semantics remain. No fabricated success if child hangs. Installed on the Mini as C:\ProgramData\NWOAS\NWOS177.EXE using the existing NWOAS Agent startup task. Prior task XML is backed up as S177-task-before.xml; original NWOS.EXE is retained. No second concurrent worker.

Actual worker source host-tested: seven scenarios including wrong disk, wrong extent, wrong serial, ambiguous match, NUL failure, CreateProcess failure, success; actual run/scratch/ACK logic compiled plain+ASan/UBSan. Native ARM64 PE builds. Hardware startup and repeated command execution passed in S180/S183/S185/S187/S189/S192 runs, including benchmark launch and output collection. Scratch selection is hardware exercised; this does not add a timeout for a hung child process.
