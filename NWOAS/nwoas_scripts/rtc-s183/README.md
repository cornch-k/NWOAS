# S183 bounded SMC / direct PMU comparison

Primary OpenBSD source discovered at https://raw.githubusercontent.com/openbsd/src/master/sys/arch/arm64/dev/aplpmu.c. SERA counter is32.16fixedpoint; RTCoffset33.15fixedpoint. Thus epoch=(counter+(offset<<1))>>16. TheS178rawvector withthisformula isUTC2026-09-09T19:26:06,~1.8saheadofhost, whileincorrectsameunitsformula yielded2031. Next SMCreadtests CLKMcounterscale directly. No guestseed, noPMUoffsetwrites.

SMCmanagementstart/quiesce messages are statechanges, so probe ONLY beforeguestlaunch. Eachoperationdeadlineup to5s, lowerproxyRPCtimeoutseparate. CLKMread6bytes verifiesSMCSRAMrange0x23fe00000..0x23ff00000: Linux t8103.dtsi smc reg[1] (reg-name sram) corresponds to ADT iop-smc-nub region-base/size. ADT smc reg[1] is a different resource; indices must not be transferred across schemas. Quiescefailureabortslaunch. Actualboundedmethods fakeMMIOtested; hardwareunverified.

First run rtc-s183-20260910-044615.524WJj rejectedbufferbecausemainmistakenlyusedADTreg[1]asLinuxreg[1]. No sharedmemory read occurred, SMCquiescedsuccessfully, Windowsbootcontinued. CorrectedonlytopologyguardagainstactualLinuxdtsi+ADTnubregion, andnowrecordsreturnedpointerbeforevalidation. Correctedhardwaretrialpending.
