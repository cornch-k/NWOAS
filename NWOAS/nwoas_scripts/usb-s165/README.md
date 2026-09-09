# S165 USB-C control preparation

HARDWARE-UNVERIFIED. No USB-C change on the running S163200us guest.
The prepared control launcher uses S16350us HV, S135 eight-core payload with
XHC1 exposed, and the prior D81/D83 USB-C initialization module. All are hash
pinned. Do not launch until the storage comparison selects that HV. If50us
fails, deliberately regenerate the control around the accepted storage image.

Compare this D83 control before reintroducing S157's stateless transfer cursor.
The latter removes a cached-pointer lifetime hazard, but can regress if the
hardware output-context dequeue pointer lags a retired ring segment. Neither
version provides a complete DMA ownership protocol. Opus review artifacts are
in ../claude-s156/opus-s165-usbc-result.*; its prose mistakenly calls this
USB-C controller FL1100 in places. USB-C here is Apple DWC3/xHCI; FL1100 is
the separate USB-A controller. Treat register/device claims accordingly.

A PnP Code0 state or absence of HSE while the user sleeps does not prove
sustained physical input. Capture controller errors and descriptor/control
activity; physical movement confirmation remains a separate validation.
