# S160 review checkpoint

Implemented in isolated m1n1_windows-s159 buildtree (name retained; candidate is S160), sourcecopy hv_vm-s160.c. Main repo src stays S158. Isolated guest_module.py; old launchers still use nvme-s130 and are untouched.

Opus4.8 design review confirmed key requirements implemented: pull targetmask before every fallbackPCI/MMIOread/write; preserve adminCQIRQ on transitions; signaturecheck rejects olderABI; eligibilityanyenabledpendingCQ; existing ORofhost/fastIRQpreserved. Main corrections to review: S159 never locallyownedmask, so S160stale-mask risk is NOT an explanation of S159fault. C registerfastpath runs underbhl, so reviewclaimunlockedconcurrentaccessisnotestablished. No rootcauseproof asserted.

Validation:7behavioraltests in test_local_mask.py pass, includingactualCregisterhandler andactualPythonfallbackfunctions plusactualController.update_irq. Existing22S149/S150sourcechecks pass againstcandidateC; theirhostsourcefixtureisoriginalS130, so the7newtestscoverS160hostfunctions. Build passeswithexisting3unusedfunctionwarnings,256blockphysicalceilingpreserved. No maskoperation executesphysicalI/O.

HVbuild/m1n1-s160-local-mask.bin2146304bytesSHA256f95bee4537f0d961b54ea3546af34100a8909e4ef9491066fc051a8d8c0ae0c7. HostmoduleSHA2565f8cf61b3ccc32fc53a40f3ea14aa3ffa5ffbd34f672e1e7092e6eb291db3c05. LauncherhashpinsbothandsetsNWOAS_NVME_FAST_MASK=1. Queries8(signature+mask),9(readcount+writecount),old0..7unchanged. OnbugcheckS160counterlineadded;noextraperiodiclogs.

Hardwarestartedusb-s160-20260910-000739.m0HOW6,TTY90748. Pendingbootandruntimevalidation. S158previousrecoveryrun235354also0x133subtype0withfarending0x10,NS1max149506ticks,NS2max427476ticks,backlog1,CQpending1;notstablebaseline. Allcrashesandcandidatefailurespreserved.
