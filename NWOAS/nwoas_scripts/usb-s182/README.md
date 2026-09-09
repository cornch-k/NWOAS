# S182 pre-Run CRCR evidence candidate

Extends S175 DART1 SID1 translation by recording actual Windows CRCR writes. Some xHC hardware reads return zero for the command ring pointer; S175 skipped validation in that case. This candidate requires both32-bit halves or one64-bit write while USBCMD.RS is clear before firstRun. HCRST clears the latch; unrelated/running writes do not modify it. Captured pointer is checked against DART mapping and stage2 likeotherDMApointers. No guestMMIOvalue modified by the latch. Failed validation suppressesfirstRun, asS175alreadydoes. ExistingS175ready latch means this is firstRun validation, notfullreset/reconnectimplementation.

263 boundarychecks plain+ASan/UBSan passed. ActualmappingC56cases passed. Candidate nativebuild SHA96da22db21c78f309d7faff2eb62ff50e12976e0eb4b67a635cc9697a4a43e78,2162688B. No hardwaretest yet. Source restored, genericbuildimage nowS182; use namedS163controlbinary.
